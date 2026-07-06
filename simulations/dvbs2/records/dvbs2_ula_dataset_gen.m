% dvbs2_ula_dataset_gen.m
%
% Generates DVB-S2 baseband receiver samples for a Uniform Linear Array (ULA)
% at a configurable direction of arrival (DOA).
%
% Requires: Phased Array System Toolbox  (phased.ULA, phased.Collector)
%           (the PL frame is constructed locally, no further toolboxes;
%            MATLAB's dvbs2WaveformGenerator from the Satellite
%            Communications Toolbox can substitute the frame generation)
%
% PL frame structure (ETSI EN 302 307-1, section 5.5):
%   SOF (26 symbols, 18D2E82h, pi/2-BPSK)  |  PLSC (64 symbols, pi/2-BPSK)
%   |  num_slots PL slots of 90 QPSK data symbols each
% The PLSC and data symbols are randomized stand-ins (frame detection only
% correlates against the SOF; PLS decoding is not in scope), pulse-shaped
% with a root-raised-cosine filter of configurable roll-off.
%
% Signal model (narrowband, d = lambda/2):
%   y_n[k] = s[k] * exp(j*n*pi*sin(DOA)) + w_n[k]
%   where s[k] is the DVB-S2 baseband PL frame and w_n is AWGN.
%
% Output (.mat):
%   rx_dataset  [N_rx x seq_len x num_frames]  complex baseband per element
%   tx_dataset  [1    x seq_len x num_frames]  transmitted baseband signal
%   params      struct                          all simulation parameters
%   (seq_len = frame_len + frame_padding; each PL frame is embedded in a
%    noise-only padded sequence, starting at sample pad_pre+1)

clear; clc;

%% ================================================================
%  Configurable Parameters
%  ================================================================

% --- DVB-S2 PL frame ---------------------------------------------
rolloff    = 0.35;      % RRC roll-off factor: 0.35 | 0.25 | 0.20
k          = 2;         % Samples per symbol
rrc_span   = 20;        % RRC filter span [symbols]
num_slots  = 2;         % PL data slots of 90 QPSK symbols each
sym_rate   = 1e6;       % Symbol rate [baud] (only scales the time axes)

% --- ULA ---------------------------------------------------------
N_rx          = 4;      % Number of receive elements
d_over_lambda = 0.5;    % Element spacing / wavelength  (lambda/2 = 0.5)
doa_deg       = 30;     % Direction of arrival [degrees from broadside; 0 = broadside]
fc            = 11.7e9; % Carrier frequency [Hz] (Ku-band downlink)

% --- Channel -----------------------------------------------------
snr_db      = 15;       % Per-element SNR [dB]  (signal power normalised to 1)
num_frames  = 100;      % Number of independent frames in the dataset
rand_seed   = 42;       % RNG seed for reproducibility
frame_padding = 300;    % Noise-only samples around each frame (split before/after)

% --- Output ------------------------------------------------------
output_dir  = 'simulations/dvbs2/records';
output_file = fullfile(output_dir, ...
    sprintf('dvbs2_sof_N%d_DOA%ddeg_SNR%ddB.mat', ...
            N_rx, round(doa_deg), round(snr_db)));

%% ================================================================
%  DVB-S2 PL Frame Configuration
%  ================================================================

f_s = k * sym_rate;                    % Sample rate [Hz]

% SOF: 26 bits 18D2E82h (MSB first), pi/2-BPSK modulated
% (EN 302 307-1, section 5.5.2; matches scframesync_init_dvbs2_sof)
sof_bits = int2bit(hex2dec('18D2E82'), 26).';
sof_syms = pi2bpsk(sof_bits);

sof_len    = numel(sof_syms);          % 26 SOF symbols
plsc_len   = 64;                       % 64 PLSC symbols
data_len   = num_slots * 90;           % 90 QPSK symbols per PL slot
num_syms   = sof_len + plsc_len + data_len;
frame_len  = num_syms * k;             % Samples per frame (symbol-rate span)

% Sequence length incl. noise-only padding (frame embedded at pad_pre offset)
pad_pre = floor(frame_padding / 2);
seq_len = frame_len + frame_padding;

% RRC pulse-shaping filter (matched by the scframesync receive filter)
rrc = rcosdesign(rolloff, rrc_span, k, 'sqrt');
rrc_delay = rrc_span/2 * k;            % Filter group delay [samples]

fprintf('DVB-S2 config: rolloff=%.2f  k=%d  slots=%d\n', rolloff, k, num_slots);
fprintf('Sample rate:    %.3f MHz\n', f_s/1e6);
fprintf('Frame length:   %d symbols = %d samples (%.1f us)\n', num_syms, frame_len, frame_len/f_s*1e6);
fprintf('Sequence length: %d samples (%d noise padding)\n\n', seq_len, frame_padding);

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
% manual steering vector exp(j*n*dphi). For the few-MHz signal bandwidth at
% Ku-band the inter-element delay is negligible vs. the sample period.
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

rx_dataset = complex(zeros(N_rx, seq_len, num_frames));
tx_dataset = complex(zeros(1,    seq_len, num_frames));

noise_std  = sqrt(10^(-snr_db/10) / 2);   % Std per I/Q component (Pnoise = 10^(-SNR/10))

fprintf('\nGenerating %d frames ', num_frames);
for frame = 1:num_frames
    if mod(frame, 10) == 0, fprintf('.'); end

    % --- PLSC stand-in: random pi/2-BPSK symbols ---
    plsc_syms = pi2bpsk(randi([0 1], 1, plsc_len));

    % --- PL data slots: random QPSK symbols ---
    data_syms = (1 - 2*randi([0 1], 1, data_len) + ...
           1j * (1 - 2*randi([0 1], 1, data_len))) / sqrt(2);

    % --- Assemble PL frame and apply RRC pulse shaping ---
    syms     = [sof_syms, plsc_syms, data_syms];
    shaped   = upfirdn(syms, rrc, k);              % [1 x num_syms*k + rrc_span*k]
    tx_frame = shaped(rrc_delay+1 : rrc_delay+frame_len);   % Trim filter delay

    % Normalise to unit average power
    tx_frame = tx_frame / sqrt(mean(abs(tx_frame).^2));

    % --- Embed frame in noise-only padded sequence ---
    tx_seq = complex(zeros(1, seq_len));
    tx_seq(pad_pre+1 : pad_pre+frame_len) = tx_frame;

    % --- Collect signal at ULA elements (phased.Collector, narrowband) ---
    rx_seq = collector(tx_seq.', [doa_deg; 0]).';      % [N_rx x seq_len]

    % --- Independent AWGN per element (over the full padded sequence) ---
    rx_seq = rx_seq + noise_std * (randn(N_rx, seq_len) + 1j*randn(N_rx, seq_len));

    rx_dataset(:, :, frame) = rx_seq;
    tx_dataset(1, :, frame) = tx_seq;
end
fprintf(' done.\n');

%% ================================================================
%  Save Dataset
%  ================================================================

if ~exist(output_dir, 'dir')
    mkdir(output_dir);
end

params = struct( ...
    'rolloff',        rolloff,        ...  % RRC roll-off factor
    'k',              k,              ...  % Samples per symbol
    'rrc_span',       rrc_span,       ...  % RRC filter span [symbols]
    'num_slots',      num_slots,      ...  % PL data slots per frame
    'sym_rate',       sym_rate,       ...  % Symbol rate [baud]
    'f_s',            f_s,            ...  % Sample rate [Hz]
    'sof_len',        sof_len,        ...  % SOF symbols
    'plsc_len',       plsc_len,       ...  % PLSC symbols
    'data_len',       data_len,       ...  % Data symbols
    'frame_len',      frame_len,      ...  % Samples per frame
    'frame_padding',  frame_padding,  ...  % Noise-only padding samples per sequence
    'pad_pre',        pad_pre,        ...  % Noise samples before the frame
    'seq_len',        seq_len,        ...  % Samples per padded sequence
    'fc',             fc,             ...  % Carrier frequency [Hz]
    'lambda',         lambda,         ...  % Carrier wavelength [m]
    'd',              d,              ...  % Element spacing [m]
    'N_rx',           N_rx,           ...  % Number of ULA elements
    'd_over_lambda',  d_over_lambda,  ...  % Element spacing / lambda
    'doa_deg',        doa_deg,        ...  % DOA [degrees from broadside]
    'doa_rad',        doa_rad,        ...  % DOA [radians]
    'dphi',           dphi,           ...  % Inter-element phase shift [rad]
    'snr_db',         snr_db,         ...  % Per-element SNR [dB]
    'num_frames',     num_frames,     ...  % Number of frames
    'rand_seed',      rand_seed       ...  % RNG seed
);

save(output_file, 'rx_dataset', 'tx_dataset', 'params', '-v7.3');

fprintf('\nDataset saved -> %s\n', output_file);
fprintf('  Frames: %d | Elements: %d | DOA: %.1f deg | SNR: %.0f dB | rolloff: %.2f\n', ...
        num_frames, N_rx, doa_deg, snr_db, rolloff);

%% ================================================================
%  Binary export for C++ import  (dvbs2_sim.cc)
%  ================================================================
% Layout: [uint32 N_rx | uint32 seq_len | uint32 num_frames]
%         [float32 re, float32 im] x seq_len, channel-major, per frame.
% Each frame is stored as its padded sequence (noise before/after the frame).
% Samples stored as single-precision (float32) to match Sample_t = complex<float>.

bin_file = strrep(output_file, '.mat', '.bin');
fid = fopen(bin_file, 'wb');
fwrite(fid, uint32(N_rx),       'uint32');
fwrite(fid, uint32(seq_len),    'uint32');
fwrite(fid, uint32(num_frames), 'uint32');
for fr = 1:num_frames
    for ch = 1:N_rx
        samps = single(squeeze(rx_dataset(ch, :, fr)));  % [1 x seq_len] single
        fwrite(fid, [real(samps); imag(samps)], 'float32');  % col-major: re0 im0 re1 im1 ...
    end
end
fclose(fid);
fprintf('Binary export  -> %s\n', bin_file);

%% ================================================================
%  Sanity-check Visualization
%  ================================================================

pl_fields = {'SOF','PLSC','Data'};
field_clrs = {'#9467bd','#2ca02c','#d62728'};
field_starts = [0, sof_len, sof_len+plsc_len] * k;

figure('Name', 'DVB-S2 ULA Dataset — Sanity Check', 'NumberTitle', 'off');
sgtitle(sprintf('DVB-S2  N_{rx}=%d  DOA=%.1f°  SNR=%.0f dB  |  rolloff=%.2f  slots=%d', ...
        N_rx, doa_deg, snr_db, rolloff, num_slots), ...
        'FontWeight', 'bold');

% --- Time domain with PL field markers (elem 1, frame 1) ---------
ax1 = subplot(2,3,1:2);
t_us = (0:seq_len-1) / f_s * 1e6;
plot(ax1, t_us, real(squeeze(rx_dataset(1,:,1))), 'b', ...
          t_us, imag(squeeze(rx_dataset(1,:,1))), 'r--');
hold(ax1, 'on');
for f = 1:numel(pl_fields)
    t_start = (pad_pre + field_starts(f)) / f_s * 1e6;
    xline(ax1, t_start, '--', 'Color', field_clrs{f}, 'Alpha', 0.7, ...
          'Label', pl_fields{f}, 'LabelOrientation', 'horizontal', ...
          'LabelVerticalAlignment', 'top', 'FontSize', 7);
end
xlabel(ax1, 'Time (\mus)'); ylabel(ax1, 'Amplitude');
title(ax1, sprintf('RX elem 1 — padded PL frame (frame 1)  [%d samples, %.1f \mus]', ...
      seq_len, seq_len/f_s*1e6));
legend(ax1, 'Re','Im','Location','northeast'); grid(ax1, 'on');

% --- PSD (elem 1, frame 1) ---------------------------------------
ax2 = subplot(2,3,3);
Nfft  = 1024;
f_MHz = (-Nfft/2:Nfft/2-1) * (f_s/Nfft) / 1e6;
psd   = fftshift(abs(fft(squeeze(rx_dataset(1,:,1)), Nfft)).^2) / (Nfft * f_s);
plot(ax2, f_MHz, 10*log10(psd));
xlabel(ax2, 'Frequency (MHz)'); ylabel(ax2, 'PSD (dB/Hz)');
title(ax2, 'RX elem 1 — PSD (frame 1)'); grid(ax2, 'on');

% --- SOF correlation (elem 1, frame 1) ---------------------------
ax3 = subplot(2,3,4);
replica = upfirdn(sof_syms, rrc, k);                    % pulse-shaped SOF replica
[xc_sof, lags_sof] = xcorr(squeeze(rx_dataset(1,:,1)), replica);
sel = lags_sof >= 0 & lags_sof < seq_len - sof_len*k;
plot(ax3, lags_sof(sel), abs(xc_sof(sel)) / max(abs(xc_sof(sel))));
xline(ax3, pad_pre - rrc_delay, 'r--', 'frame start', 'LabelVerticalAlignment', 'bottom');
xlabel(ax3, 'Lag (samples)'); ylabel(ax3, '|R(\tau)| (norm.)');
title(ax3, 'SOF correlation (elem 1, frame 1)'); grid(ax3, 'on');

% --- Cross-correlation between adjacent elements (frame 1) -------
ax4 = subplot(2,3,5);
x0 = squeeze(rx_dataset(1,:,1));
x1 = squeeze(rx_dataset(2,:,1));
[xc, lags] = xcorr(x1, x0, 80, 'normalized');
stem(ax4, lags, abs(xc), 'filled', 'MarkerSize', 3);
xlabel(ax4, 'Lag (samples)'); ylabel(ax4, '|R_{10}(\tau)|');
title(ax4, 'Cross-correlation elem 2 vs elem 1 (frame 1)'); grid(ax4, 'on');

% --- Conventional Beamformer DOA scan using the PLHEADER ---------
ax5 = subplot(2,3,6);
plh_range = (pad_pre + 1):(pad_pre + (sof_len+plsc_len)*k);
X_plh = squeeze(rx_dataset(:, plh_range, 1));   % [N_rx x N_plh]
R_plh = X_plh * X_plh' / numel(plh_range);      % Spatial covariance [N_rx x N_rx]

doa_scan = -90:0.5:90;
P_cbf = zeros(1, numel(doa_scan));
for kk = 1:numel(doa_scan)
    a_k       = exp(1j * (0:N_rx-1).' * 2*pi * d_over_lambda * sin(deg2rad(doa_scan(kk))));
    P_cbf(kk) = real(a_k' * R_plh * a_k);
end
P_cbf_dB = 10*log10(P_cbf / max(P_cbf));
plot(ax5, doa_scan, P_cbf_dB, 'b');
hold(ax5, 'on');
xline(ax5, doa_deg, 'r--', sprintf('DOA=%.1f°', doa_deg), 'LabelVerticalAlignment','bottom');
xlabel(ax5, 'DOA (°)'); ylabel(ax5, 'Normalised power (dB)');
title(ax5, 'CBF DOA scan (PLHEADER snapshot)');
ylim(ax5, [-40 5]); grid(ax5, 'on');

%% ================================================================
%  Local Functions
%  ================================================================

% pi/2-BPSK mapping (EN 302 307-1, section 5.5.2):
% even symbol index (0-based): (1-2y)*(+1+j)/sqrt(2)
% odd  symbol index          : (1-2y)*(-1+j)/sqrt(2)
function syms = pi2bpsk(bits)
    b    = 1 - 2*double(bits(:).');
    syms = complex(zeros(1, numel(b)));
    syms(1:2:end) = b(1:2:end) * ( 1 + 1j) / sqrt(2);
    syms(2:2:end) = b(2:2:end) * (-1 + 1j) / sqrt(2);
end
