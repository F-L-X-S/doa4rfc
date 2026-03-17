#
# @file music-spectrum.py
# @author Felix Schuelke (flxscode@gmail.com)
# 
# @brief This file contains the implementation of the MUSIC-Algorithm for estimating the Direction-of-arrival (DoA)
# of a signal impinging on an antenna array. The Data is received via a ZeroMQ socket and is processed in real-time.
# The received Channel State Information (CSI) is formatted as a matrix with dimensions (size, n_boards, n_rows, n_antennas, subcarriers).
# The MUSIC-Implementation is based on the ESPARGOS demo project https://github.com/ESPARGOS/pyespargos
# @version 0.1
# @date 2025-05-20

import pathlib
import sys

sys.path.append(str(pathlib.Path(__file__).absolute().parents[2]))

import numpy as np

import PyQt6.QtWidgets
import PyQt6.QtCharts
import PyQt6.QtCore
import PyQt6.QtQml
import zmq
import struct

ANTENNAS_PER_ROW = 2

class MusicSpectrum(PyQt6.QtWidgets.QApplication):
	def pollSocket(self):
     	# Drain the socket: collect all messages 
		messages = []
		while True:
			try:
				msg = self.socket.recv(zmq.NOBLOCK)
				messages.append(msg)
			except zmq.Again:
				break  # No more messages available

		#process messages 
		all_data = []
		for msg in messages:
				# Header: 3 x uint32 → 12 bytes
				n_measurements, num_channels, samples_per_channel = struct.unpack("III", msg[:12])
				
    			# Load Data (complex values)
				data = np.frombuffer(msg[12:], dtype=np.complex64)
    
				# Transform to  (num_channels, samples_per_channel) 
				try:
					reshaped = data.reshape((n_measurements, num_channels, samples_per_channel))
				except ValueError:
					return  # skip invalid reshape
 
				all_data.append(reshaped)  
  

		if not all_data:
			return
      
		# stack along n_measurements axis
		stacked = np.concatenate(all_data, axis=0)
      
		# shape : (size, n_arrays, n_rows, n_antennas, subcarriers)
		self.csi = stacked[:, np.newaxis, np.newaxis, :, :]
   
		# modify steering vectors
		if num_channels != self.antennas_per_row:
			self.antennas_per_row = num_channels
			self.steering_vectors = np.exp(-1.0j * np.outer(np.pi * np.sin(self.scanning_angles), np.arange(self.antennas_per_row)))

		
	def __init__(self, argv):
		super().__init__(argv)

		# Qt setup
		self.aboutToQuit.connect(self.onAboutToQuit)
		self.engine = PyQt6.QtQml.QQmlApplicationEngine()
		
		# ZMQ socket setup
		context = zmq.Context()
		self.socket = context.socket(zmq.PULL)
		self.socket.bind("tcp://*:5555")
		self.poller = zmq.Poller()
		self.poller.register(self.socket, zmq.POLLIN)
		self.csi = None
		self.antennas_per_row = ANTENNAS_PER_ROW

		# Initialize MUSIC scanning angles, steering vectors
   		# steering vectors are the phases of the received Signal as function of angle theta 
		self.scanning_angles = np.linspace(-np.pi / 2, np.pi / 2, 1800) 
		self.steering_vectors = np.exp(-1.0j * np.outer(np.pi * np.sin(self.scanning_angles), np.arange(self.antennas_per_row)))
		self.spatial_spectrum = None

		# Poll CSI from socket
		self.timer = PyQt6.QtCore.QTimer()
		self.timer.timeout.connect(self.pollSocket)
		self.timer.start(50) # 100ms


	def exec(self):
		context = self.engine.rootContext()
		context.setContextProperty("backend", self)

		qmlFile = pathlib.Path(__file__).resolve().parent / "music-spectrum-ui.qml"
		self.engine.load(qmlFile.as_uri())
		if not self.engine.rootObjects():
			return -1

		return super().exec()

	@PyQt6.QtCore.pyqtSlot(PyQt6.QtCharts.QLineSeries, PyQt6.QtCharts.QValueAxis)
	def updateSpatialSpectrum(self, series, axis):
		if self.csi is None:
			return

		# compute the covariance matrix (complex inner product between indices i and j (n_antennas axis))
  		# R_ij = sum_d sum_b sum_r sum_s CSI[d,b,r,i,s] * conj( CSI[d,b,r,j,s] )
		R = np.einsum("dbris,dbrjs->ij", self.csi, np.conj(self.csi))
  
		# eigenvalue decomposition
		eig_val, eig_vec = np.linalg.eig(R)
  
		# sort eigenvalues and eigenvectors in decreasing order
		order = np.argsort(eig_val)[::-1]
  
		# ignore the eigenvector of the largest eigenvalue => noise subspace
		Qn = eig_vec[:,order][:,1:]
  
		# compute the spatial spectrum
		spatial_spectrum_linear = 1 / np.linalg.norm(np.einsum("ae,ra->er", np.conj(Qn), self.steering_vectors), axis = 0)
		spatial_spectrum_log = 20 * np.log10(spatial_spectrum_linear)

		axis.setMin(np.min(spatial_spectrum_log) - 1)
		axis.setMax(max(np.max(spatial_spectrum_log), axis.max()))

		data = [PyQt6.QtCore.QPointF(np.rad2deg(angle), power) for angle, power in zip(self.scanning_angles, spatial_spectrum_log)]
		series.replace(data)

	def onAboutToQuit(self):
		self.engine.deleteLater()

	@PyQt6.QtCore.pyqtProperty(list, constant=True)
	def scanningAngles(self):
		return self.scanning_angles.tolist()

app = MusicSpectrum(sys.argv)
sys.exit(app.exec())