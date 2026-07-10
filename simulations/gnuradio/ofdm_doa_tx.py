"""
ofdm_doa_tx.py

GNU Radio flowgraph that generates OFDM frames for all antennas of a
half-wavelength ULA, phase-shifted according to a DoA of 30 degrees, and
streams the multi-channel IQ samples to the doa4rfc application
(gnuradio_sim) via the zmq_if_sink block (gnuradio/doa4rfc_zmq_if_sink.py).

The frames are generated with liquid-DSP's ofdmflexframegen (loaded via
ctypes from the project build tree), so they match the ofdmflexframesync
configuration in gnuradio_sim.cc (M=64, cp_len=16, taper_len=4, default
subcarrier allocation, QPSK, CRC-16, no FEC). Each channel transmits the
same frame multiplied by the steering-vector element
exp(-j*pi*sin(DoA)*n), embedded in AWGN at SNR_DB.

Usage (start gnuradio_sim first, then run with GNU Radio's Python):
	cmake --build build/ClangDebug --target gnuradio_sim
	./build/ClangDebug/gnuradio_sim
	$(head -1 $(which gnuradio-companion) | cut -c3-) simulations/gnuradio/ofdm_doa_tx.py
"""

import ctypes
import sys
import time
from pathlib import Path

import numpy as np
from gnuradio import blocks, gr

# Make the doa4rfc GRC block importable
REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "gnuradio"))
from doa4rfc_zmq_if_sink import zmq_if_sink

# Definition of the transmission-settings
NUM_CHANNELS = 4                        # Number of ULA channels — must match NUM_CHANNELS in gnuradio_sim.cc
DOA_DEG = 30.0                          # Direction of arrival [degrees], half-wavelength ULA
SNR_DB = 40.0                           # Signal-to-noise ratio (dB)
SAMPLE_RATE = 10e3                      # Throttle rate [samples/s] — paces the stream
SEQUENCE_LEN = 50000                    # Samples per repeated sequence (frame period = SEQUENCE_LEN / SAMPLE_RATE)
FRAME_OFFSET = 150                      # Noise-only samples before the frame

# OFDM framegen parameters — must match the SyncWorker configuration in gnuradio_sim.cc
M = 64                                  # Number of subcarriers
CP_LEN = 16                             # Cyclic prefix length
TAPER_LEN = 4                           # Window taper length
PAYLOAD_LEN = 4                         # Payload length (bytes)

# ZMQ-socket for export to the doa4rfc application (IMPORT_INTERFACE in gnuradio_sim.cc)
ENDPOINT = "tcp://127.0.0.1:5554"

# liquid-DSP shared library from the project build tree
LIQUID_LIB = REPO_ROOT / "build" / "ClangDebug" / "external" / "liquid-dsp" / "libliquid.dylib"


class OfdmFlexFrameGenProps(ctypes.Structure):
	"""Mirror of liquid's ofdmflexframegenprops_s"""
	_fields_ = [
		("check", ctypes.c_uint),
		("fec0", ctypes.c_uint),
		("fec1", ctypes.c_uint),
		("mod_scheme", ctypes.c_uint),
	]


def generate_ofdm_frame():
	"""Generate one liquid OFDM frame (complex64 baseband samples)"""
	liquid = ctypes.CDLL(str(LIQUID_LIB))
	liquid.ofdmflexframegen_create.restype = ctypes.c_void_p
	liquid.ofdmflexframegen_create.argtypes = [
		ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
		ctypes.c_char_p, ctypes.POINTER(OfdmFlexFrameGenProps)]
	liquid.ofdmflexframegen_assemble.argtypes = [
		ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
	liquid.ofdmflexframegen_write.argtypes = [
		ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
	liquid.ofdmflexframegen_destroy.argtypes = [ctypes.c_void_p]

	# Set modulation scheme, data validity check and forward error-correction schemes
	fgprops = OfdmFlexFrameGenProps()
	liquid.ofdmflexframegenprops_init_default(ctypes.byref(fgprops))
	fgprops.check = liquid.liquid_getopt_str2crc(b"crc16")
	fgprops.fec0 = liquid.liquid_getopt_str2fec(b"none")
	fgprops.fec1 = liquid.liquid_getopt_str2fec(b"none")
	fgprops.mod_scheme = liquid.liquid_getopt_str2mod(b"qpsk")

	# Assemble frame (NULL subcarrier allocation = ofdmframe_init_default_sctype)
	fg = liquid.ofdmflexframegen_create(M, CP_LEN, TAPER_LEN, None, ctypes.byref(fgprops))
	header = bytes(range(8))
	payload = np.random.default_rng(0).bytes(PAYLOAD_LEN)
	liquid.ofdmflexframegen_assemble(fg, header, payload, PAYLOAD_LEN)

	# Write one OFDM symbol per iteration until the frame is complete
	symbol_len = M + CP_LEN
	tx = np.zeros(0, dtype=np.complex64)
	frame_complete = 0
	while not frame_complete:
		buf = np.zeros(symbol_len, dtype=np.complex64)
		frame_complete = liquid.ofdmflexframegen_write(
			fg, buf.ctypes.data_as(ctypes.c_void_p), symbol_len)
		tx = np.concatenate([tx, buf])

	liquid.ofdmflexframegen_destroy(fg)
	return tx


def build_channel_sequences():
	"""Frame + steering phase per channel, embedded in AWGN"""
	frame = generate_ofdm_frame()
	if FRAME_OFFSET + len(frame) > SEQUENCE_LEN:
		raise ValueError("SEQUENCE_LEN too short for generated frame")

	# Steering vector of the half-wavelength ULA (matches music/estimators.py)
	steering = np.exp(-1j * np.pi * np.sin(np.deg2rad(DOA_DEG)) * np.arange(NUM_CHANNELS))

	# AWGN with SNR_DB relative to the frame power
	rng = np.random.default_rng(1)
	sigma = np.sqrt(np.mean(np.abs(frame) ** 2)) * 10.0 ** (-SNR_DB / 20.0)
	sequences = []
	for ch in range(NUM_CHANNELS):
		seq = (rng.standard_normal(SEQUENCE_LEN) + 1j * rng.standard_normal(SEQUENCE_LEN)) * sigma / np.sqrt(2.0)
		seq[FRAME_OFFSET:FRAME_OFFSET + len(frame)] += frame * steering[ch]
		sequences.append(seq.astype(np.complex64))
	print(f"Frame: {len(frame)} samples, DoA {DOA_DEG} deg, "
		f"period {SEQUENCE_LEN / SAMPLE_RATE:.1f} s")
	return sequences


class OfdmDoaTx(gr.top_block):
	"""vector_source (repeating) -> throttle -> zmq_if_sink, one path per channel"""

	def __init__(self):
		gr.top_block.__init__(self, "doa4rfc OFDM DoA TX")
		sequences = build_channel_sequences()
		# Keep Python references to all blocks: Python-implemented blocks
		# (zmq_if_sink) must not be garbage-collected while the flowgraph runs
		self.sink = zmq_if_sink(ENDPOINT, NUM_CHANNELS)
		self.sources = []
		self.throttles = []
		for ch in range(NUM_CHANNELS):
			src = blocks.vector_source_c(sequences[ch].tolist(), True)
			throttle = blocks.throttle(gr.sizeof_gr_complex, SAMPLE_RATE)
			self.sources.append(src)
			self.throttles.append(throttle)
			self.connect(src, throttle, (self.sink, ch))


if __name__ == "__main__":
	tb = OfdmDoaTx()
	tb.start()
	print(f"Streaming {NUM_CHANNELS} channels to {ENDPOINT} — Ctrl-C to stop")
	try:
		while True:
			time.sleep(0.5)
	except KeyboardInterrupt:
		pass
	tb.stop()
	tb.wait()
