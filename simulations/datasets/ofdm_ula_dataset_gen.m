% ofdm_ula_dataset_gen.m
%
% Generates baseband receiver samples for an OFDM signal impinging on a
% Uniform Linear Array (ULA) from a configurable direction of arrival (DOA).
%
% Signal model (narrowband, d = lambda/2):
%   y_n[k] = s[k] * exp(j*n*pi*sin(DOA)) + w_n[k]
%   where s[k] is the OFDM baseband signal and w_n is AWGN.
%   Spatial collection implemented via phased.Collector (Phased Array System Toolbox).
%
% Requires: Communications Toolbox, Phased Array System Toolbox
%
% Subcarrier indexing uses DC-centred convention (index 1 = f_{-M/2},
% index M/2+1 = DC, index M = f_{M/2-1}). ifftshift converts to MATLAB
% FFT ordering before ifft.
%
% Output (.mat):
%   rx_dataset  [N_rx x frame_len x num_frames]  complex baseband per element
%   tx_dataset  [1    x frame_len x num_frames]  transmitted baseband signal
%   params      struct                            all simulation parameters

clear; clc;

%% ================================================================
%  Configurable Parameters
%  ================================================================

% --- OFDM --------------------------------------------------------
M             = 64;     % Number of subcarriers
cp_len        = 16;     % Cyclic prefix length [samples]
num_data_syms = 14;     % OFDM data symbols per frame (no protocol preamble)
mod_order     = 4;      % Constellation size: 2=BPSK, 4=QPSK, 16=16-QAM, 64=64-QAM
f_s           = 20e6;   % Baseband sample rate [Hz]

% Subcarrier allocation (DC-centred indices, 1-based)
n_guard_low   = 6;      % Null subcarriers at lower spectral edge (most-negative freq)
n_guard_high  = 5;      % Null subcarriers at upper spectral edge (most-positive freq)
pilot_spacing = 8;      % Pilot subcarrier spacing among active scs (0 = no pilots)

% --- ULA ---------------------------------------------------------
N_rx          = 4;      % Number of receive elements
d_over_lambda = 0.5;    % Element spacing / wavelength (lambda/2 = 0.5)
doa_deg       = 30;     % Direction of arrival [degrees from broadside; 0 = broadside]
fc            = 2.4e9;  % Carrier frequency [Hz] (determines physical element spacing)

% --- Channel -----------------------------------------------------
snr_db        = 20;     % Per-element SNR [dB], signal power normalised to 1
num_frames    = 100;    % Number of independent frames in the dataset
rand_seed     = 42;     % RNG seed for reproducibility

% --- Output ------------------------------------------------------
output_dir  = 'simulations/datasets';
output_file = fullfile(output_dir, ...
    sprintf('ofdm_ula_N%d_DOA%ddeg_SNR%ddB.mat', N_rx, round(doa_deg), round(snr_db)));

%% ================================================================
%  Derived Parameters & Subcarrier Allocation
%  ================================================================

symbol_len = M + cp_len;
frame_len  = num_data_syms * symbol_len;

% DC-centred subcarrier type vector (length M, 1-based)
%  Index 1 = f_{-M/2},  Index M/2+1 = DC (f=0),  Index M = f_{M/2-1}
dc_idx  = M/2 + 1;
sc_type = zeros(1, M);                              % 0 = null by default
sc_type(n_guard_low+1  : dc_idx-1)    = 2;          % Negative-freq active band
sc_type(dc_idx+1       : M-n_guard_high) = 2;       % Positive-freq active band

if pilot_spacing > 0
    active = find(sc_type == 2);
    sc_type(active(1:pilot_spacing:end)) = 1;        % Thin out to pilot
end

data_sc_idx  = find(sc_type == 2);                  % 1-based DC-centred indices
N_data_sc    = numel(data_sc_idx);
bits_per_sym = log2(mod_order);

fprintf('Subcarrier summary: %d total | %d null | %d pilot | %d data\n', ...
    M, sum(sc_type == 0), sum(sc_type == 1), N_data_sc);

%% ================================================================
%  ULA Array & Signal Collector  (Phased Array System Toolbox)
%  ================================================================

c       = physconst('LightSpeed');
lambda  = c / fc;
d       = d_over_lambda * lambda;               % Element spacing [m]
doa_rad = deg2rad(doa_deg);
dphi    = 2*pi * d_over_lambda * sin(doa_rad); % Inter-element phase shift [rad] (for visualization)

ula = phased.ULA('NumElements', N_rx, 'ElementSpacing', d);

% phased.Collector applies narrowband plane-wave phase shifts identical to the
% manual steering vector exp(j*n*dphi). For our parameters the max inter-element
% delay (~0.1 ns) is negligible vs. the sample period (~50 ns at 20 MHz).
collector = phased.Collector( ...
    'Sensor',             ula, ...
    'PropagationSpeed',   c,   ...
    'OperatingFrequency', fc,  ...
    'Wavefront',          'Plane');

fprintf('ULA: %d elements  d = %.4f m (%.1f lambda)  DOA = %.1f deg  dphi = %.4f rad\n', ...
        N_rx, d, d_over_lambda, doa_deg, dphi);

%% ================================================================
%  Dataset Generation
%  ================================================================

rng(rand_seed);

rx_dataset = complex(zeros(N_rx, frame_len, num_frames));
tx_dataset = complex(zeros(1,    frame_len, num_frames));

noise_std = sqrt(10^(-snr_db/10) / 2);              % Std per I/Q component (Pnoise = 10^(-SNR/10))

freq_sym = zeros(M, 1);                              % Reusable frequency-domain buffer

for frame = 1:num_frames

    % --- Random payload bits ---
    n_bits   = N_data_sc * num_data_syms * bits_per_sym;
    bits     = randi([0 1], n_bits, 1);
    sym_idx  = bi2de(reshape(bits, bits_per_sym, []).', 'left-msb');

    % --- Modulate data subcarriers ---
    switch mod_order
        case 2,  data_syms = pskmod(sym_idx, 2);
        case 4,  data_syms = pskmod(sym_idx, 4, pi/4);
        case 16, data_syms = qammod(sym_idx, 16, 'UnitAveragePower', true);
        case 64, data_syms = qammod(sym_idx, 64, 'UnitAveragePower', true);
        otherwise
            error('Unsupported mod_order %d. Use 2, 4, 16, or 64.', mod_order);
    end
    data_syms = reshape(data_syms, N_data_sc, num_data_syms);  % [N_data_sc x num_data_syms]

    % --- OFDM modulation (IFFT + cyclic prefix) ---
    tx_frame = zeros(1, frame_len);
    for s = 1:num_data_syms
        freq_sym(:)           = 0;
        freq_sym(data_sc_idx) = data_syms(:, s);
        % ifftshift: DC-centred -> MATLAB FFT ordering  |  sqrt(M): energy normalisation
        time_sym = sqrt(M) * ifft(ifftshift(freq_sym));
        cp       = time_sym(end-cp_len+1 : end);
        tx_frame((s-1)*symbol_len + (1:symbol_len)) = [cp; time_sym].';
    end

    % Normalise to unit average power
    tx_frame = tx_frame / sqrt(mean(abs(tx_frame).^2));

    % --- Collect signal at ULA elements (phased.Collector, narrowband) ---
    rx_frame = collector(tx_frame.', [doa_deg; 0]).';  % [N_rx x frame_len]

    % --- AWGN: independent per element ---
    rx_frame = rx_frame + noise_std * (randn(N_rx, frame_len) + 1j*randn(N_rx, frame_len));

    rx_dataset(:, :, frame) = rx_frame;
    tx_dataset(1, :, frame) = tx_frame;
end

%% ================================================================
%  Save Dataset
%  ================================================================

if ~exist(output_dir, 'dir')
    mkdir(output_dir);
end

params = struct( ...
    'M',             M,              ...  % Number of subcarriers
    'cp_len',        cp_len,         ...  % Cyclic prefix length [samples]
    'num_data_syms', num_data_syms,  ...  % Data symbols per frame
    'mod_order',     mod_order,      ...  % Modulation order
    'f_s',           f_s,            ...  % Sample rate [Hz]
    'n_guard_low',   n_guard_low,    ...  % Lower guard subcarriers
    'n_guard_high',  n_guard_high,   ...  % Upper guard subcarriers
    'pilot_spacing', pilot_spacing,  ...  % Pilot spacing (0 = none)
    'symbol_len',    symbol_len,     ...  % Samples per symbol (with CP)
    'frame_len',     frame_len,      ...  % Samples per frame
    'N_data_sc',     N_data_sc,      ...  % Number of data subcarriers
    'data_sc_idx',   data_sc_idx,    ...  % DC-centred data SC indices
    'sc_type',       sc_type,        ...  % Subcarrier type vector
    'fc',            fc,             ...  % Carrier frequency [Hz]
    'lambda',        lambda,         ...  % Carrier wavelength [m]
    'd',             d,              ...  % Element spacing [m]
    'N_rx',          N_rx,           ...  % Number of ULA elements
    'd_over_lambda', d_over_lambda,  ...  % Element spacing / lambda
    'doa_deg',       doa_deg,        ...  % DOA [degrees from broadside]
    'doa_rad',       doa_rad,        ...  % DOA [radians]
    'dphi',          dphi,           ...  % Inter-element phase shift [rad]
    'snr_db',        snr_db,         ...  % Per-element SNR [dB]
    'num_frames',    num_frames,     ...  % Number of frames
    'rand_seed',     rand_seed       ...  % RNG seed
);

save(output_file, 'rx_dataset', 'tx_dataset', 'params', '-v7.3');

fprintf('\nDataset saved -> %s\n', output_file);
fprintf('  Frames:          %d\n',         num_frames);
fprintf('  ULA elements:    %d\n',         N_rx);
fprintf('  DOA:             %.1f deg\n',   doa_deg);
fprintf('  SNR:             %.0f dB\n',    snr_db);
fprintf('  OFDM:            M=%d  CP=%d  %d-QAM  %d data syms\n', ...
        M, cp_len, mod_order, num_data_syms);
fprintf('  Data subcarriers:%d / %d\n',   N_data_sc, M);
fprintf('  Frame length:    %d samples\n', frame_len);

%% ================================================================
%  Sanity-check Visualization
%  ================================================================

figure('Name', 'OFDM ULA Dataset — Sanity Check', 'NumberTitle', 'off');
sgtitle(sprintf('N_{rx}=%d  DOA=%.1f°  SNR=%.0f dB  |  M=%d  CP=%d  %d-QAM  %d syms', ...
        N_rx, doa_deg, snr_db, M, cp_len, mod_order, num_data_syms), 'FontWeight', 'bold');

% --- Time domain (element 1, frame 1) ---
ax1 = subplot(2,3,1);
t_us = (0:frame_len-1) / f_s * 1e6;
plot(ax1, t_us, real(squeeze(rx_dataset(1,:,1))), 'b', ...
          t_us, imag(squeeze(rx_dataset(1,:,1))), 'r--');
xlabel(ax1, 'Time (\mus)'); ylabel(ax1, 'Amplitude');
title(ax1, 'RX elem 1 — time domain (frame 1)');
legend(ax1, 'Re', 'Im'); grid(ax1, 'on');

% --- Power spectrum (element 1, frame 1) ---
ax2 = subplot(2,3,2);
Nfft  = 512;
f_MHz = (-Nfft/2:Nfft/2-1) * (f_s / Nfft) / 1e6;
psd   = fftshift(abs(fft(squeeze(rx_dataset(1,:,1)), Nfft)).^2) / (Nfft * f_s);
plot(ax2, f_MHz, 10*log10(psd));
xlabel(ax2, 'Frequency (MHz)'); ylabel(ax2, 'PSD (dB/Hz)');
title(ax2, 'RX elem 1 — PSD (frame 1)'); grid(ax2, 'on');

% --- Subcarrier allocation (DC-centred) ---
ax3 = subplot(2,3,3);
bar(ax3, 1:M, sc_type);
xlabel(ax3, 'Subcarrier index (DC-centred)'); ylabel(ax3, 'Type');
title(ax3, sprintf('Subcarrier allocation (M=%d)', M));
yticks(ax3, [0 1 2]); yticklabels(ax3, {'Null','Pilot','Data'}); grid(ax3, 'on');

% --- Inter-element phase (first symbol of first frame, skip CP) ---
ax4 = subplot(2,3,4);
sym_samps   = squeeze(rx_dataset(:, cp_len+1:symbol_len, 1));  % [N_rx x M]
elem_phasor = mean(sym_samps, 2);                               % Average across subcarriers
meas_phase  = wrapToPi(angle(elem_phasor) - angle(elem_phasor(1)));
ideal_phase = wrapToPi((0:N_rx-1).' * dphi);
plot(ax4, 0:N_rx-1, rad2deg(ideal_phase), 'b--o', 'DisplayName', 'Ideal');
hold(ax4, 'on');
plot(ax4, 0:N_rx-1, rad2deg(meas_phase),  'r-x',  'DisplayName', 'Measured');
xlabel(ax4, 'Element index'); ylabel(ax4, 'Phase (°)');
title(ax4, 'Inter-element phase (sym 1, frame 1)');
legend(ax4); grid(ax4, 'on');

% --- Cross-correlation elem 2 vs elem 1 (first frame) ---
ax5 = subplot(2,3,5);
x0 = squeeze(rx_dataset(1,:,1));
x1 = squeeze(rx_dataset(2,:,1));
[xc, lags] = xcorr(x1, x0, 60, 'normalized');
stem(ax5, lags, abs(xc), 'filled', 'MarkerSize', 3);
xlabel(ax5, 'Lag (samples)'); ylabel(ax5, '|R_{10}(\tau)|');
title(ax5, 'Cross-correlation elem 2 vs elem 1'); grid(ax5, 'on');

% --- Conventional beamformer (CBF) DOA scan (first frame, first symbol) ---
ax6 = subplot(2,3,6);
doa_scan = -90:0.5:90;
X = squeeze(rx_dataset(:, cp_len+1:symbol_len, 1));  % [N_rx x M] snapshot
R = X * X' / M;                                       % Spatial covariance
P_cbf = zeros(1, numel(doa_scan));
for k = 1:numel(doa_scan)
    a_k      = exp(1j * (0:N_rx-1).' * 2*pi * d_over_lambda * sin(deg2rad(doa_scan(k))));
    P_cbf(k) = real(a_k' * R * a_k);
end
P_cbf_dB = 10*log10(P_cbf / max(P_cbf));
plot(ax6, doa_scan, P_cbf_dB, 'b');
hold(ax6, 'on');
xline(ax6, doa_deg, 'r--', sprintf('DOA=%.1f°', doa_deg), 'LabelVerticalAlignment', 'bottom');
xlabel(ax6, 'DOA (°)'); ylabel(ax6, 'Normalised power (dB)');
title(ax6, 'Conventional beamformer spectrum (CBF)');
ylim(ax6, [-40 5]); grid(ax6, 'on');
