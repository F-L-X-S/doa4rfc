#
# @file music-spectrum.py
# @author Felix Schuelke (flxscode@gmail.com)
#
# @brief Real-time DoA spectrum and frame viewer. The multi-channel frame data (time-domain
# samples and demodulated symbols) is received via a ZeroMQ socket (see receiver.py) and
# visualized per exported frame: time-domain amplitude, magnitude, FFT spectrum and symbol
# constellation. The DoA spatial spectrum is computed by an interchangeable estimation
# algorithm (see estimators.py, default: MUSIC) selected via the --estimator argument.
# All plots are interactive pyqtgraph plots: drag to pan, scroll/right-drag to zoom,
# 'A' button to re-enable auto-range, right-click for context menu (export to CSV/PNG).
# The received Channel State Information (CSI) is formatted as a matrix with dimensions (size, n_boards, n_rows, n_antennas, subcarriers).
# The MUSIC-Implementation is based on the ESPARGOS demo project https://github.com/ESPARGOS/pyespargos
# @version 0.3
# @date 2026-07-08

import argparse
import sys

import numpy as np
import pyqtgraph as pg
from PyQt6 import QtCore, QtGui, QtWidgets

from receiver import FrameReceiver
from estimators import ESTIMATORS

ANTENNAS_PER_ROW = 2

# Dark theme matching the previous GUI
BACKGROUND_COLOR = "#11191e"
FOREGROUND_COLOR = "#e0e0e0"
CHANNEL_COLORS = ["#4fc3f7", "#ffb74d", "#81c784", "#e57373", "#ba68c8", "#4db6ac", "#fff176", "#f06292"]

pg.setConfigOption("background", BACKGROUND_COLOR)
pg.setConfigOption("foreground", FOREGROUND_COLOR)
pg.setConfigOptions(antialias=True)


class DoaViewer(QtWidgets.QMainWindow):
	def __init__(self, estimator):
		super().__init__()

		# ZMQ socket setup
		self.receiver = FrameReceiver("tcp://*:5555")
		self.csi = None

		# DoA estimation algorithm (interchangeable, see estimators.py)
		self.estimator = estimator

		# Frame counters of the last rendered frame per message type
		self.rendered_samples_frame = 0
		self.rendered_symbols_frame = 0

		# Per-channel plot curves, created lazily to match the received channel count
		self.time_curves = []   # (real, imag) tuples
		self.mag_curves = []
		self.fft_curves = []
		self.const_curves = []

		self.setWindowTitle("DoA Spectrum & Frame Viewer")
		self.resize(1200, 800)

		# Full screen management
		QtGui.QShortcut(QtGui.QKeySequence("F11"), self, self.toggleFullScreen)
		QtGui.QShortcut(QtGui.QKeySequence("Esc"), self, self.close)

		# ---------------------- Plot layout ----------------------
		layout = pg.GraphicsLayoutWidget()
		self.setCentralWidget(layout)

		# DoA spatial spectrum (full width)
		self.doa_plot = layout.addPlot(row=0, col=0, colspan=2,
			title=f"{self.estimator.name} Azimuth Spatial Spectrum")
		self.doa_plot.setLabel("bottom", "Scanning Vector Angle Θ [degrees]")
		self.doa_plot.setLabel("left", "P(Θ) [dB]")
		self.doa_plot.showGrid(x=True, y=True, alpha=0.3)
		self.doa_plot.setXRange(-90, 90)
		self.doa_curve = self.doa_plot.plot(pen=pg.mkPen("#4fc3f7", width=2))

		# Time-domain samples of the last exported frame (real: solid, imag: dashed)
		self.time_plot = layout.addPlot(row=1, col=0, title="Frame Samples")
		self.time_plot.setLabel("bottom", "Sample Index")
		self.time_plot.setLabel("left", "Amplitude")
		self.time_plot.showGrid(x=True, y=True, alpha=0.3)
		self.time_plot.addLegend(offset=(10, 10))

		# Magnitude of the last exported frame
		self.mag_plot = layout.addPlot(row=1, col=1, title="Frame Samples Magnitude")
		self.mag_plot.setLabel("bottom", "Sample Index")
		self.mag_plot.setLabel("left", "Magnitude")
		self.mag_plot.showGrid(x=True, y=True, alpha=0.3)

		# Magnitude spectrum (FFT) of the last exported frame
		self.fft_plot = layout.addPlot(row=2, col=0, title="Frame Spectrum")
		self.fft_plot.setLabel("bottom", "Normalized Frequency")
		self.fft_plot.setLabel("left", "Magnitude [dB]")
		self.fft_plot.showGrid(x=True, y=True, alpha=0.3)

		# Constellation of the demodulated symbols of the last exported frame
		self.const_plot = layout.addPlot(row=2, col=1, title="Received Symbols")
		self.const_plot.setLabel("bottom", "In-Phase")
		self.const_plot.setLabel("left", "Quadrature")
		self.const_plot.showGrid(x=True, y=True, alpha=0.3)
		self.const_plot.setAspectLocked(True)
		self.const_plot.addLegend(offset=(10, 10))

		# Poll CSI from socket and refresh the plots
		self.timer = QtCore.QTimer(self)
		self.timer.timeout.connect(self.refresh)
		self.timer.start(50)

	def toggleFullScreen(self):
		if self.isFullScreen():
			self.showNormal()
		else:
			self.showFullScreen()

	def channelPen(self, channel, dashed=False):
		color = CHANNEL_COLORS[channel % len(CHANNEL_COLORS)]
		style = QtCore.Qt.PenStyle.DashLine if dashed else QtCore.Qt.PenStyle.SolidLine
		return pg.mkPen(color, width=1, style=style)

	def ensureChannelCurves(self, num_channels):
		"""(Re-)create the per-channel curves when the received channel count changes."""
		if len(self.time_curves) == num_channels:
			return
		for plot, curves in ((self.time_plot, [c for pair in self.time_curves for c in pair]),
		                     (self.mag_plot, self.mag_curves),
		                     (self.fft_plot, self.fft_curves)):
			for curve in curves:
				plot.removeItem(curve)
			if plot.legend is not None:
				plot.legend.clear()

		self.time_curves = [(self.time_plot.plot(pen=self.channelPen(ch), name=f"CH{ch} re"),
		                     self.time_plot.plot(pen=self.channelPen(ch, dashed=True), name=f"CH{ch} im"))
		                    for ch in range(num_channels)]
		self.mag_curves = [self.mag_plot.plot(pen=self.channelPen(ch)) for ch in range(num_channels)]
		self.fft_curves = [self.fft_plot.plot(pen=self.channelPen(ch)) for ch in range(num_channels)]

	def ensureConstellationCurves(self, num_channels):
		if len(self.const_curves) == num_channels:
			return
		for curve in self.const_curves:
			self.const_plot.removeItem(curve)
		if self.const_plot.legend is not None:
			self.const_plot.legend.clear()

		self.const_curves = [self.const_plot.plot(pen=None, symbol="o", symbolSize=5,
		                                          symbolPen=None, symbolBrush=CHANNEL_COLORS[ch % len(CHANNEL_COLORS)],
		                                          name=f"CH{ch}")
		                     for ch in range(num_channels)]

	def refresh(self):
     	# Drain the socket: collect all sample frames (symbols are tracked inside the receiver)
		sample_frames = self.receiver.poll()

		if sample_frames:
			# stack along n_measurements axis
			stacked = np.concatenate(sample_frames, axis=0)

			# shape : (size, n_arrays, n_rows, n_antennas, subcarriers)
			self.csi = stacked[:, np.newaxis, np.newaxis, :, :]

			# modify steering vectors
			num_channels = stacked.shape[1]
			if num_channels != self.estimator.num_channels:
				self.estimator.set_num_channels(num_channels)

			self.updateSpatialSpectrum()

		if self.receiver.samples_frame_count != self.rendered_samples_frame:
			self.rendered_samples_frame = self.receiver.samples_frame_count
			self.updateFrameCharts()

		if self.receiver.symbols_frame_count != self.rendered_symbols_frame:
			self.rendered_symbols_frame = self.receiver.symbols_frame_count
			self.updateConstellation()

	def updateSpatialSpectrum(self):
		# compute the spatial spectrum with the selected estimation algorithm
		spatial_spectrum_log = self.estimator.compute(self.csi)
		self.doa_curve.setData(np.rad2deg(self.estimator.scanning_angles), spatial_spectrum_log)

	def updateFrameCharts(self):
		samples = self.receiver.latest_samples
		self.ensureChannelCurves(samples.shape[0])

		indices = np.arange(samples.shape[1])

		# magnitude spectrum [dB] over normalized frequency
		spectrum = np.fft.fftshift(np.fft.fft(samples, axis=1), axes=1)
		magnitude_db = 20 * np.log10(np.abs(spectrum) + 1e-12)
		frequencies = np.linspace(-0.5, 0.5, samples.shape[1], endpoint=False)

		for ch in range(samples.shape[0]):
			self.time_curves[ch][0].setData(indices, samples[ch].real)
			self.time_curves[ch][1].setData(indices, samples[ch].imag)
			self.mag_curves[ch].setData(indices, np.abs(samples[ch]))
			self.fft_curves[ch].setData(frequencies, magnitude_db[ch])

	def updateConstellation(self):
		symbols = self.receiver.latest_symbols
		self.ensureConstellationCurves(symbols.shape[0])

		for ch in range(symbols.shape[0]):
			self.const_curves[ch].setData(symbols[ch].real, symbols[ch].imag)


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description="Real-time DoA spectrum and frame viewer")
	parser.add_argument("--estimator", default="music", choices=sorted(ESTIMATORS), help="DoA estimation algorithm")
	args, qt_args = parser.parse_known_args()

	app = QtWidgets.QApplication(sys.argv[:1] + qt_args)
	viewer = DoaViewer(ESTIMATORS[args.estimator](ANTENNAS_PER_ROW))
	viewer.show()
	sys.exit(app.exec())
