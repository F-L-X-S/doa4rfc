"""
doa4rfc_zmq_if_sink.py

GNU Radio sink block that streams complex IQ samples to the doa4rfc
application via a ZeroMQ PUSH socket (doa4rfc binds a PULL socket on its
IMPORT_INTERFACE, e.g. tcp://127.0.0.1:5554).

Wire format (matches ZmqSender/ZmqReceiver in include/interfaces/zmq):
	header:  4 x uint32 little-endian
	         [msg_type, numMeasurements, numChannels, samplesPerChannel]
	payload: complex64 (float32 re/im interleaved),
	         row-major [measurement][channel][sample]

Each call to work() is sent as one message with msg_type = 0 (samples)
and numMeasurements = 1. Messages are sent non-blocking: if doa4rfc is
not running (or cannot keep up), chunks are dropped instead of stalling
the flowgraph.
"""

import struct

import numpy as np
import zmq
from gnuradio import gr

# Message type tag (first header word), see ZmqMsgType in zmq_if.h
MSG_TYPE_SAMPLES = 0


class zmq_if_sink(gr.sync_block):
	"""Stream complex IQ samples of num_channels ULA channels to doa4rfc via ZMQ"""

	def __init__(self, endpoint="tcp://127.0.0.1:5554", num_channels=4):
		self.num_channels = int(num_channels)
		gr.sync_block.__init__(
			self,
			name="doa4rfc_zmq_if_sink",
			in_sig=[np.complex64] * self.num_channels,
			out_sig=None,
		)
		self.endpoint = endpoint
		self.context = None
		self.socket = None
		self.dropped = 0

	def start(self):
		# Open the PUSH socket when the flowgraph starts
		self.context = zmq.Context()
		self.socket = self.context.socket(zmq.PUSH)
		self.socket.setsockopt(zmq.SNDHWM, 8)   # keep latency low, drop instead of buffering
		self.socket.setsockopt(zmq.LINGER, 0)   # do not block teardown on unsent messages
		self.socket.connect(self.endpoint)
		return True

	def stop(self):
		# Close the socket when the flowgraph stops
		if self.socket is not None:
			self.socket.close()
			self.socket = None
		if self.context is not None:
			self.context.term()
			self.context = None
		return True

	def work(self, input_items, output_items):
		n = len(input_items[0])

		# Header: [msg_type, numMeasurements, numChannels, samplesPerChannel]
		header = struct.pack("<IIII", MSG_TYPE_SAMPLES, 1, self.num_channels, n)

		# Payload: channel-major complex64
		payload = b"".join(
			np.ascontiguousarray(input_items[ch][:n], dtype=np.complex64).tobytes()
			for ch in range(self.num_channels)
		)

		# Non-blocking send: drop the chunk if no receiver is connected or the
		# high-water mark is reached (keeps the flowgraph running)
		try:
			self.socket.send(header + payload, zmq.NOBLOCK)
		except zmq.Again:
			self.dropped += 1
			if self.dropped == 1 or self.dropped % 256 == 0:
				self.logger.warn(
					f"doa4rfc_zmq_if_sink: no receiver on {self.endpoint}, "
					f"dropped {self.dropped} chunks"
				)

		return n
