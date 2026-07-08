#
# @file estimators.py
# @author Felix Schuelke (flxscode@gmail.com)
#
# @brief Interchangeable DoA estimation algorithms for the DoA GUI. Every estimator
# derives from DoaEstimator and computes a spatial spectrum [dB] over the scanning
# angles from the 5-D CSI matrix (size, n_boards, n_rows, n_antennas, subcarriers).
# The MUSIC implementation is based on the ESPARGOS demo project https://github.com/ESPARGOS/pyespargos
# @version 0.1
# @date 2026-07-08

from abc import ABC, abstractmethod

import numpy as np


class DoaEstimator(ABC):
	"""Base class for DoA estimation algorithms operating on a uniform linear array
	with half-wavelength element spacing."""

	name = "DoA"

	def __init__(self, num_channels, num_scanning_angles=1800):
		self.scanning_angles = np.linspace(-np.pi / 2, np.pi / 2, num_scanning_angles)
		self.num_channels = None
		self.steering_vectors = None
		self.set_num_channels(num_channels)

	def set_num_channels(self, num_channels):
		"""Rebuild the steering vectors for the given number of array elements.
		steering vectors are the phases of the received Signal as function of angle theta"""
		self.num_channels = num_channels
		self.steering_vectors = np.exp(-1.0j * np.outer(np.pi * np.sin(self.scanning_angles), np.arange(self.num_channels)))

	@abstractmethod
	def compute(self, csi):
		"""Compute the spatial spectrum [dB] per scanning angle from the 5-D CSI matrix
		(size, n_boards, n_rows, n_antennas, subcarriers)."""


class MusicEstimator(DoaEstimator):
	"""MUSIC (MUltiple SIgnal Classification) spectrum for a single signal source."""

	name = "MUSIC"

	def compute(self, csi):
		# compute the covariance matrix (complex inner product between indices i and j (n_antennas axis))
  		# R_ij = sum_d sum_b sum_r sum_s CSI[d,b,r,i,s] * conj( CSI[d,b,r,j,s] )
		R = np.einsum("dbris,dbrjs->ij", csi, np.conj(csi))

		# eigenvalue decomposition
		eig_val, eig_vec = np.linalg.eig(R)

		# sort eigenvalues and eigenvectors in decreasing order
		order = np.argsort(eig_val)[::-1]

		# ignore the eigenvector of the largest eigenvalue => noise subspace
		Qn = eig_vec[:,order][:,1:]

		# compute the spatial spectrum
		spatial_spectrum_linear = 1 / np.linalg.norm(np.einsum("ae,ra->er", np.conj(Qn), self.steering_vectors), axis = 0)
		spatial_spectrum_log = 20 * np.log10(spatial_spectrum_linear)

		return spatial_spectrum_log


# Registry of selectable estimators (extend when adding new algorithms)
ESTIMATORS = {
	"music": MusicEstimator,
}
