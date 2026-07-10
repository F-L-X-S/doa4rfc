"""
ofdm_doa_tx.py

GNU Radio flowgraph that generates OFDM frames for all antennas of a
half-wavelength ULA, phase-shifted according to a DoA of 30 degrees, and
streams the multi-channel IQ samples to the doa4rfc application
(gnuradio_sim) via the zmq_if_sink block (gnuradio/doa4rfc_zmq_if_sink.py).

The frames are generated with GNU Radio's own OFDM blocks only (the same
blocks digital.ofdm_tx is built from: ofdm_carrier_allocator_cvc, fft_vcc,
ofdm_cyclic_prefixer) — no liquid-DSP on the TX side. Frame structure and
carrier layout use gr-digital's 802.11a-style defaults (fft_len 64, cp_len
16, 48 data carriers, pilots at +/-7/+/-21 with the 802.11 polarity
scrambling), detected by the configurable wlanframesync in gnuradio_sim.cc:

	[STF | STF | LTF | payload symbols...], each 80 samples (16 CP + 64)

- STF sync word: BPSK*sqrt(2) on every 4th carrier only, giving a
  16-sample time-domain periodicity (stf_period) that the per-symbol
  cyclic prefix preserves seamlessly (CP length = one STF period, as in
  the 802.11 L-STF). It is sent twice (stock digital.ofdm_tx sends one
  80-sample sync symbol, too short for the synchronizer's S0 gain/CFO
  estimation states).
- LTF sync word: BPSK on all 52 active carriers, sent once (ltf_count 1).
The sign patterns below are fixed (numpy RandomState(42)) and hardcoded
identically in gnuradio_sim.cc — keep both in sync.

Usage (start gnuradio_sim first, then run with GNU Radio's Python):
	cmake --build build/ClangDebug --target gnuradio_sim
	./build/ClangDebug/gnuradio_sim
	$(head -1 $(which gnuradio-companion) | cut -c3-) simulations/gnuradio/ofdm_doa_tx.py
"""

import sys
import time
from pathlib import Path

import numpy as np
import pmt
from gnuradio import blocks, digital, fft, gr

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

# OFDM frame parameters — must match the wlanframesync configuration in gnuradio_sim.cc
FFT_LEN = 64                            # Number of subcarriers
CP_LEN = 16                             # Cyclic prefix length
PAYLOAD_LEN = 24                        # Payload length (bytes) — 96 QPSK symbols = 2 full OFDM symbols
LEN_TAG = "packet_len"                  # Tagged-stream length key

# 802.11a-style carrier layout (gr-digital ofdm_tx defaults)
OCCUPIED_CARRIERS = (list(range(-26, -21)) + list(range(-20, -7)) + list(range(-6, 0))
	+ list(range(1, 7)) + list(range(8, 21)) + list(range(22, 27)))
PILOT_CARRIERS = (-21, -7, 7, 21)
PILOT_SCRAMBLE = (
	1, 1, 1, 1, -1, -1, -1, 1, -1, -1, -1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1, 1, 1, 1, 1, 1, -1, 1,
	1, 1, -1, 1, 1, -1, -1, 1, 1, 1, -1, 1, -1, -1, -1, 1, -1, 1, -1, -1, 1, -1, -1, 1, 1, 1, 1, 1, -1, -1, 1, 1,
	-1, -1, 1, -1, 1, -1, 1, 1, -1, -1, -1, 1, 1, -1, -1, -1, -1, 1, -1, -1, 1, -1, 1, 1, 1, 1, -1, 1, -1, 1, -1, 1,
	-1, -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1, 1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1)
PILOT_SYMBOLS = tuple((x, x, x, -x) for x in PILOT_SCRAMBLE)

# Fixed sync-word sign patterns (numpy RandomState(42)) — hardcoded identically in gnuradio_sim.cc
STF_K = (-24, -20, -16, -12, -8, -4, 4, 8, 12, 16, 20, 24)
STF_B = (1, -1, 1, 1, 1, -1, 1, 1, 1, -1, 1, 1)
LTF_K = tuple(k for k in range(-26, 27) if k != 0)
LTF_B = (1, 1, -1, 1, -1, -1, -1, 1, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, 1, 1, -1, -1, -1, 1, -1, 1,
	1, 1, 1, 1, -1, -1, -1, -1, -1, 1, -1, -1, 1, -1, 1, -1, 1, -1, -1, 1, 1, 1, 1, 1, 1, 1)

# ZMQ-socket for export to the doa4rfc application (IMPORT_INTERFACE in gnuradio_sim.cc)
ENDPOINT = "tcp://127.0.0.1:5554"


def make_sync_word(carriers, signs, scale):
	"""Frequency-domain sync word in fftshifted order (ofdm_carrier_allocator convention)"""
	word = np.zeros(FFT_LEN, dtype=complex)
	for k, b in zip(carriers, signs):
		word[k % FFT_LEN] = b * scale
	return np.fft.fftshift(word).tolist()


def generate_ofdm_frame():
	"""Generate one OFDM frame (complex64 baseband samples) using GNU Radio blocks only"""
	stf = make_sync_word(STF_K, STF_B, np.sqrt(2.0))
	ltf = make_sync_word(LTF_K, LTF_B, 1.0)

	payload = bytes(range(PAYLOAD_LEN))
	tag = gr.tag_t()
	tag.offset = 0
	tag.key = pmt.intern(LEN_TAG)
	tag.value = pmt.from_long(PAYLOAD_LEN)

	qpsk = digital.constellation_qpsk()
	tb = gr.top_block("frame generator")
	src = blocks.vector_source_b(list(payload), False, 1, [tag])
	repack = blocks.repack_bits_bb(8, qpsk.bits_per_symbol(), LEN_TAG)
	mod = digital.chunks_to_symbols_bc(qpsk.points())
	allocator = digital.ofdm_carrier_allocator_cvc(
		FFT_LEN,
		occupied_carriers=(OCCUPIED_CARRIERS,),
		pilot_carriers=(PILOT_CARRIERS,),
		pilot_symbols=PILOT_SYMBOLS,
		sync_words=[stf, stf, ltf],
		len_tag_key=LEN_TAG)
	ffter = fft.fft_vcc(FFT_LEN, False, (), True)
	prefixer = digital.ofdm_cyclic_prefixer(FFT_LEN, FFT_LEN + CP_LEN, 0, LEN_TAG)
	sink = blocks.vector_sink_c()
	tb.connect(src, repack, mod, allocator, ffter, prefixer, sink)
	tb.run()
	return np.array(sink.data(), dtype=np.complex64)


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
