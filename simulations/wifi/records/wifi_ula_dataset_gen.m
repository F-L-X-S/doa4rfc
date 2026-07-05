% wifi_ula_dataset_gen.m
%
% Generates IEEE 802.11n (HT-mixed format) baseband receiver samples for a
% Uniform Linear Array (ULA) at a configurable direction of arrival (DOA).
%
% Requires: WLAN Toolbox              (wlanHTConfig, wlanWaveformGenerator,
%                                      wlanFieldIndices, wlanSampleRate)
%           Phased Array System Toolbox  (phased.ULA, phased.Collector)
%
% PPDU structure (HT-mixed format):
%   L-STF | L-LTF | L-SIG | HT-SIG | HT-STF | HT-LTF(s) | HT-Data
%
% Signal model (narrowband, d = lambda/2):
%   y_n[k] = s[k] * exp(j*n*pi*sin(DOA)) + w_n[k]
%   where s[k] is the 802.11n baseband PPDU and w_n is AWGN.
%
% Output (.mat):
%   rx_dataset  [N_rx x seq_len x num_frames]  complex baseband per element
%   tx_dataset  [1    x seq_len x num_frames]  transmitted baseband signal
%   params      struct                          all simulation parameters
%   (seq_len = frame_len + frame_padding; each PPDU is embedded in a
%    noise-only padded sequence, starting at sample pad_pre+1)

clear; clc;

%% ================================================================
%  Configurable Parameters
%  ================================================================

% --- IEEE 802.11n PPDU (HT-mixed, single spatial stream) ---------
chan_bw     = 'CBW20';  % Channel bandwidth: 'CBW20' (20 MHz) | 'CBW40' (40 MHz)
mcs         = 7;        % MCS index (Nss = 1):
                        %   0: BPSK   1/2  → 6.5 Mbps (LGI)
                        %   1: QPSK   1/2  → 13 Mbps
                        %   2: QPSK   3/4  → 19.5 Mbps
                        %   3: 16-QAM 1/2  → 26 Mbps
                        %   4: 16-QAM 3/4  → 39 Mbps
                        %   5: 64-QAM 2/3  → 52 Mbps
                        %   6: 64-QAM 3/4  → 58.5 Mbps
                        %   7: 64-QAM 5/6  → 65 Mbps
guard_int   = 'Long';   % Guard interval: 'Long' (800 ns) | 'Short' (400 ns)
psdu_length = 10;     % PSDU payload length [bytes]

% --- ULA ---------------------------------------------------------
N_rx          = 4;      % Number of receive elements
d_over_lambda = 0.5;    % Element spacing / wavelength  (lambda/2 = 0.5)
doa_deg       = 30;     % Direction of arrival [degrees from broadside; 0 = broadside]
fc            = 2.4121e9; % Carrier frequency [Hz] (matches CARRIER_FREQUENCY in wifi_sim.cc)

% --- Channel -----------------------------------------------------
snr_db      = 20;       % Per-element SNR [dB]  (signal power normalised to 1)
num_frames  = 100;      % Number of independent frames (PPDUs) in the dataset
rand_seed   = 42;       % RNG seed for reproducibility
frame_padding = 300;    % Noise-only samples around each frame (split before/after,
                        % matches FRAME_PADDING in music_sim.cc)

% --- Output ------------------------------------------------------
output_dir  = 'simulations/wifi/records';
output_file = fullfile(output_dir, ...
    sprintf('wifi11n_%s_MCS%d_N%d_DOA%ddeg_SNR%ddB.mat', ...
            strrep(chan_bw,'CBW',''), mcs, N_rx, round(doa_deg), round(snr_db)));

%% ================================================================
%  802.11n HT Configuration  (WLAN Toolbox)
%  ================================================================

cfg = wlanHTConfig;
cfg.ChannelBandwidth    = chan_bw;
cfg.NumTransmitAntennas = 1;
cfg.NumSpaceTimeStreams  = 1;
cfg.SpatialMapping      = 'Direct';
cfg.MCS                 = mcs;
cfg.GuardInterval       = guard_int;
cfg.PSDULength          = psdu_length;

f_s = wlanSampleRate(cfg);            % Nominal sample rate [Hz]
ind = wlanFieldIndices(cfg);          % Sample-index ranges of each PPDU field

% Determine frame length from a dummy generation
tx_trial  = wlanWaveformGenerator(zeros(8*psdu_length, 1), cfg);
frame_len = numel(tx_trial);

% Sequence length incl. noise-only padding (frame embedded at pad_pre offset)
pad_pre = floor(frame_padding / 2);
seq_len = frame_len + frame_padding;

fprintf('802.11n config: %s  MCS=%d  GI=%s  PSDU=%d B\n', ...
        chan_bw, mcs, guard_int, psdu_length);
fprintf('Sample rate:    %.0f MHz\n', f_s/1e6);
fprintf('Frame length:   %d samples (%.1f us)\n', frame_len, frame_len/f_s*1e6);
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
% manual steering vector exp(j*n*dphi). For 802.11n at 2.4 GHz the max
% inter-element delay (~0.1 ns) is negligible vs. the sample period (50 ns).
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

    % --- Random PSDU bits ---
    txPSDU = randi([0 1], 8*psdu_length, 1);

    % --- Generate full 802.11n PPDU  (WLAN Toolbox) ---
    tx_frame = wlanWaveformGenerator(txPSDU, cfg).';   % [1 x frame_len]

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
    'chan_bw',        chan_bw,        ...  % Channel bandwidth string
    'mcs',            mcs,            ...  % MCS index
    'guard_int',      guard_int,      ...  % Guard interval string
    'psdu_length',    psdu_length,    ...  % PSDU length [bytes]
    'f_s',            f_s,            ...  % Sample rate [Hz]
    'frame_len',      frame_len,      ...  % Samples per PPDU
    'frame_padding',  frame_padding,  ...  % Noise-only padding samples per sequence
    'pad_pre',        pad_pre,        ...  % Noise samples before the frame
    'seq_len',        seq_len,        ...  % Samples per padded sequence
    'field_indices',  ind,            ...  % PPDU field sample-index ranges (relative to frame start, add pad_pre)
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
fprintf('  Frames: %d | Elements: %d | DOA: %.1f deg | SNR: %.0f dB | MCS: %d\n', ...
        num_frames, N_rx, doa_deg, snr_db, mcs);

%% ================================================================
%  Binary export for C++ import  (wifi_sim.cc)
%  ================================================================
% Layout: [uint32 N_rx | uint32 seq_len | uint32 num_frames]
%         [float32 re, float32 im] x seq_len, channel-major, per frame.
% Each frame is stored as its padded sequence (noise before/after the PPDU).
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

ppdu_fields = {'LSTF','LLTF','LSIG','HTSIG','HTSTF','HTLTF','HTData'};
field_clrs  = {'#9467bd','#2ca02c','#17becf','#bcbd22','#7f7f7f','#1f77b4','#d62728'};

figure('Name', '802.11n ULA Dataset — Sanity Check', 'NumberTitle', 'off');
sgtitle(sprintf('IEEE 802.11n  N_{rx}=%d  DOA=%.1f°  SNR=%.0f dB  |  %s  MCS%d  GI=%s  PSDU=%d B', ...
        N_rx, doa_deg, snr_db, chan_bw, mcs, guard_int, psdu_length), ...
        'FontWeight', 'bold');

% --- Time domain with PPDU field markers (elem 1, frame 1) -------
ax1 = subplot(2,3,1:2);
t_us = (0:seq_len-1) / f_s * 1e6;
plot(ax1, t_us, real(squeeze(rx_dataset(1,:,1))), 'b', ...
          t_us, imag(squeeze(rx_dataset(1,:,1))), 'r--');
hold(ax1, 'on');
for f = 1:numel(ppdu_fields)
    fn = ppdu_fields{f};
    if isfield(ind, fn)
        t_start = (pad_pre + ind.(fn)(1) - 1) / f_s * 1e6;
        xline(ax1, t_start, '--', 'Color', field_clrs{f}, 'Alpha', 0.7, ...
              'Label', fn, 'LabelOrientation', 'horizontal', ...
              'LabelVerticalAlignment', 'top', 'FontSize', 7);
    end
end
xlabel(ax1, 'Time (\mus)'); ylabel(ax1, 'Amplitude');
title(ax1, sprintf('RX elem 1 — padded PPDU (frame 1)  [%d samples, %.1f \mus]', ...
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

% --- PPDU field durations ----------------------------------------
ax3 = subplot(2,3,4);
present = cellfun(@(fn) isfield(ind, fn), ppdu_fields);
f_names = ppdu_fields(present);
f_dur   = cellfun(@(fn) (ind.(fn)(2) - ind.(fn)(1) + 1) / f_s * 1e6, f_names);
hb = barh(ax3, 1:numel(f_names), f_dur, 'FaceColor', 'flat');
for f = 1:numel(f_names)
    hb.CData(f,:) = sscanf(field_clrs{f}(2:end), '%2x%2x%2x').'/255;
end
yticks(ax3, 1:numel(f_names)); yticklabels(ax3, f_names);
xlabel(ax3, 'Duration (\mus)');
title(ax3, sprintf('PPDU field durations  (total %.1f \mus)', frame_len/f_s*1e6));
grid(ax3, 'on');

% --- Cross-correlation between adjacent elements (frame 1) -------
ax4 = subplot(2,3,5);
x0 = squeeze(rx_dataset(1,:,1));
x1 = squeeze(rx_dataset(2,:,1));
[xc, lags] = xcorr(x1, x0, 80, 'normalized');
stem(ax4, lags, abs(xc), 'filled', 'MarkerSize', 3);
xlabel(ax4, 'Lag (samples)'); ylabel(ax4, '|R_{10}(\tau)|');
title(ax4, 'Cross-correlation elem 2 vs elem 1 (frame 1)'); grid(ax4, 'on');

% --- Conventional Beamformer DOA scan using L-STF (known preamble)
ax5 = subplot(2,3,6);
stf_range = (pad_pre + ind.LSTF(1)):(pad_pre + ind.LSTF(2));
X_stf = squeeze(rx_dataset(:, stf_range, 1));   % [N_rx x N_stf]
R_stf = X_stf * X_stf' / numel(stf_range);      % Spatial covariance [N_rx x N_rx]

doa_scan = -90:0.5:90;
P_cbf = zeros(1, numel(doa_scan));
for k = 1:numel(doa_scan)
    a_k      = exp(1j * (0:N_rx-1).' * 2*pi * d_over_lambda * sin(deg2rad(doa_scan(k))));
    P_cbf(k) = real(a_k' * R_stf * a_k);
end
P_cbf_dB = 10*log10(P_cbf / max(P_cbf));
plot(ax5, doa_scan, P_cbf_dB, 'b');
hold(ax5, 'on');
xline(ax5, doa_deg, 'r--', sprintf('DOA=%.1f°', doa_deg), 'LabelVerticalAlignment','bottom');
xlabel(ax5, 'DOA (°)'); ylabel(ax5, 'Normalised power (dB)');
title(ax5, 'CBF DOA scan (L-STF snapshot)');
ylim(ax5, [-40 5]); grid(ax5, 'on');
