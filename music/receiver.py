#
# @file receiver.py
# @author Felix Schuelke (flxscode@gmail.com)
#
# @brief ZeroMQ frame receiver for the DoA GUI. Binds a PULL socket and parses the
# messages exported by the C++ ZmqSender (see include/interfaces/zmq/src/zmq_if.cc).
# Wire format: header 4 x uint32 [msg_type, n_measurements, num_channels, samples_per_channel]
# followed by the payload as interleaved float32 (complex64), row-major
# [measurement][channel][sample]. msg_type: 0 = time-domain samples, 1 = constellation symbols.
# @version 0.1
# @date 2026-07-08

import struct

import numpy as np
import zmq

MSG_TYPE_SAMPLES = 0
MSG_TYPE_SYMBOLS = 1

HEADER_FORMAT = "IIII"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)


class FrameReceiver:
	def __init__(self, endpoint="tcp://*:5555"):
		self.context = zmq.Context()
		self.socket = self.context.socket(zmq.PULL)
		self.socket.bind(endpoint)

		# Most recent frame of each message type, shaped (num_channels, samples_per_channel)
		self.latest_samples = None
		self.latest_symbols = None

		# Frame counters to detect new frames per message type
		self.samples_frame_count = 0
		self.symbols_frame_count = 0

	def poll(self):
		"""Drain the socket and return all sample frames received in this drain,
		each shaped (n_measurements, num_channels, samples_per_channel)."""
		sample_frames = []
		while True:
			try:
				msg = self.socket.recv(zmq.NOBLOCK)
			except zmq.Again:
				break  # No more messages available

			parsed = self._parse(msg)
			if parsed is None:
				continue
			msg_type, frame = parsed

			if msg_type == MSG_TYPE_SAMPLES:
				sample_frames.append(frame)
				self.latest_samples = frame[-1]  # last measurement (num_channels, samples_per_channel)
				self.samples_frame_count += 1
			elif msg_type == MSG_TYPE_SYMBOLS:
				self.latest_symbols = frame[-1]
				self.symbols_frame_count += 1

		return sample_frames

	def _parse(self, msg):
		"""Parse a single message, returns (msg_type, data) with data shaped
		(n_measurements, num_channels, samples_per_channel) or None if invalid."""
		if len(msg) < HEADER_SIZE:
			return None
		msg_type, n_measurements, num_channels, samples_per_channel = struct.unpack(HEADER_FORMAT, msg[:HEADER_SIZE])

		data = np.frombuffer(msg[HEADER_SIZE:], dtype=np.complex64)
		try:
			reshaped = data.reshape((n_measurements, num_channels, samples_per_channel))
		except ValueError:
			return None  # skip invalid reshape

		return msg_type, reshaped
