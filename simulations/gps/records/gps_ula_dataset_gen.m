% gps_ula_dataset_gen.m
%
% Generates GPS L1 C/A baseband receiver samples for a Uniform Linear Array
% (ULA) at a configurable direction of arrival (DOA).
%
% Requires: Phased Array System Toolbox  (phased.ULA, phased.Collector)
%           (the C/A Gold code is generated locally, no further toolboxes)
%
% Signal structure (IS-GPS-200, L1 C/A):
%   BPSK-spread 1023-chip Gold code at 1.023 Mchip/s (1 ms period),
%   repeated num_periods times per frame (nav-data bit constant within a
%   frame; one bit spans 20 code periods at 50 bps). Chips are rectangular.
%
% Signal model (narrowband, d = lambda/2):
%   y_n[k] = s[k] * exp(j*n*pi*sin(DOA)) + w_n[k]
%   where s[k] is the C/A baseband signal and w_n is AWGN.
%
% Output (.mat):
%   rx_dataset  [N_rx x seq_len x num_frames]  complex baseband per element
%   tx_dataset  [1    x seq_len x num_frames]  transmitted baseband signal
%   params      struct                          all simulation parameters
%   (seq_len = frame_len + frame_padding; each frame is embedded in a
%    noise-only padded sequence, starting at sample pad_pre+1)

clear; clc;

%% ================================================================
%  Configurable Parameters
%  ================================================================

% --- GPS L1 C/A --------------------------------------------------
prn         = 1;        % Satellite PRN number (1..32)
k           = 2;        % Samples per chip (f_s = k * 1.023 MHz)
num_periods = 2;        % Code periods per frame (period 1: preamble,
                        % further periods: payload captured by scframesync)
doppler_hz  = 1000;     % Carrier Doppler shift [Hz] (max +/-5 kHz for LEO pass)

% --- ULA ---------------------------------------------------------
N_rx          = 4;      % Number of receive elements
d_over_lambda = 0.5;    % Element spacing / wavelength  (lambda/2 = 0.5)
doa_deg       = 30;     % Direction of arrival [degrees from broadside; 0 = broadside]
fc            = 1575.42e6; % Carrier frequency [Hz] (GPS L1)

% --- Channel -----------------------------------------------------
snr_db      = 10;       % Per-element SNR [dB]  (signal power normalised to 1;
                        % NOTE: real GPS is ~-20 dB pre-despreading, the
                        % 1023-chip correlation adds ~30 dB processing gain)
num_frames  = 100;      % Number of independent frames in the dataset
rand_seed   = 42;       % RNG seed for reproducibility
frame_padding = 300;    % Noise-only samples around each frame (split before/after)

% --- Output ------------------------------------------------------
output_dir  = 'simulations/gps/records';
output_file = fullfile(output_dir, ...
    sprintf('gps_l1ca_PRN%d_N%d_DOA%ddeg_SNR%ddB.mat', ...
            prn, N_rx, round(doa_deg), round(snr_db)));

%% ================================================================
%  GPS L1 C/A Baseband Configuration
%  ================================================================

chip_rate = 1.023e6;                  % C/A chip rate [Hz]
f_s       = k * chip_rate;            % Sample rate [Hz]
code      = gps_ca_code(prn);         % 1023-chip Gold code, BPSK (+/-1)
code_len  = numel(code);              % 1023 chips

frame_len = num_periods * code_len * k;   % Samples per frame

% Sequence length incl. noise-only padding (frame embedded at pad_pre offset)
pad_pre = floor(frame_padding / 2);
seq_len = frame_len + frame_padding;

fprintf('GPS L1 C/A config: PRN=%d  k=%d  periods=%d  Doppler=%.0f Hz\n', ...
        prn, k, num_periods, doppler_hz);
fprintf('Sample rate:    %.3f MHz\n', f_s/1e6);
fprintf('Frame length:   %d samples (%.1f ms)\n', frame_len, frame_len/f_s*1e3);
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
% manual steering vector exp(j*n*dphi). For the 2 MHz C/A bandwidth at L1 the
% inter-element delay is negligible vs. the sample period.
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

    % --- Random nav-data bit per code period (50 bps bit spans 20 periods;
    %     independent bits here exercise the payload polarity) ---
    nav_bits = 1 - 2*randi([0 1], 1, num_periods);

    % --- Spread nav bits with the C/A code, rectangular chips ---
    chips    = reshape(code(:) * nav_bits, 1, []);    % [1 x num_periods*1023]
    tx_frame = repelem(complex(chips), 1, k);         % [1 x frame_len], k samples/chip

    % --- Apply carrier Doppler shift ---
    t        = (0:frame_len-1) / f_s;
    tx_frame = tx_frame .* exp(1j*2*pi*doppler_hz*t);

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
    'prn',            prn,            ...  % Satellite PRN number
    'k',              k,              ...  % Samples per chip
    'num_periods',    num_periods,    ...  % Code periods per frame
    'doppler_hz',     doppler_hz,     ...  % Carrier Doppler shift [Hz]
    'chip_rate',      chip_rate,      ...  % C/A chip rate [Hz]
    'f_s',            f_s,            ...  % Sample rate [Hz]
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
fprintf('  Frames: %d | Elements: %d | DOA: %.1f deg | SNR: %.0f dB | PRN: %d\n', ...
        num_frames, N_rx, doa_deg, snr_db, prn);

%% ================================================================
%  Binary export for C++ import  (gps_sim.cc)
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

figure('Name', 'GPS L1 C/A ULA Dataset — Sanity Check', 'NumberTitle', 'off');
sgtitle(sprintf('GPS L1 C/A  N_{rx}=%d  DOA=%.1f°  SNR=%.0f dB  |  PRN=%d  Doppler=%.0f Hz', ...
        N_rx, doa_deg, snr_db, prn, doppler_hz), ...
        'FontWeight', 'bold');

% --- Time domain (elem 1, frame 1) -------------------------------
ax1 = subplot(2,3,1:2);
t_ms = (0:seq_len-1) / f_s * 1e3;
plot(ax1, t_ms, real(squeeze(rx_dataset(1,:,1))), 'b', ...
          t_ms, imag(squeeze(rx_dataset(1,:,1))), 'r--');
hold(ax1, 'on');
for p = 0:num_periods-1
    t_start = (pad_pre + p*code_len*k) / f_s * 1e3;
    xline(ax1, t_start, '--', 'Color', '#2ca02c', 'Alpha', 0.7, ...
          'Label', sprintf('period %d', p+1), 'LabelOrientation', 'horizontal', ...
          'LabelVerticalAlignment', 'top', 'FontSize', 7);
end
xlabel(ax1, 'Time (ms)'); ylabel(ax1, 'Amplitude');
title(ax1, sprintf('RX elem 1 — padded frame (frame 1)  [%d samples, %.1f ms]', ...
      seq_len, seq_len/f_s*1e3));
legend(ax1, 'Re','Im','Location','northeast'); grid(ax1, 'on');

% --- PSD (elem 1, frame 1) ---------------------------------------
ax2 = subplot(2,3,3);
Nfft  = 1024;
f_MHz = (-Nfft/2:Nfft/2-1) * (f_s/Nfft) / 1e6;
psd   = fftshift(abs(fft(squeeze(rx_dataset(1,:,1)), Nfft)).^2) / (Nfft * f_s);
plot(ax2, f_MHz, 10*log10(psd));
xlabel(ax2, 'Frequency (MHz)'); ylabel(ax2, 'PSD (dB/Hz)');
title(ax2, 'RX elem 1 — PSD (frame 1)'); grid(ax2, 'on');

% --- C/A code correlation (elem 1, frame 1) ----------------------
ax3 = subplot(2,3,4);
replica = repelem(code(:).', 1, k);                     % local code, k samples/chip
[xc_ca, lags_ca] = xcorr(squeeze(rx_dataset(1,:,1)), replica);
sel = lags_ca >= 0 & lags_ca < seq_len - code_len*k;
plot(ax3, lags_ca(sel), abs(xc_ca(sel)) / (code_len*k));
xline(ax3, pad_pre, 'r--', 'frame start', 'LabelVerticalAlignment', 'bottom');
xlabel(ax3, 'Lag (samples)'); ylabel(ax3, '|R(\tau)| (norm.)');
title(ax3, 'C/A code correlation (elem 1, frame 1)'); grid(ax3, 'on');

% --- Cross-correlation between adjacent elements (frame 1) -------
ax4 = subplot(2,3,5);
x0 = squeeze(rx_dataset(1,:,1));
x1 = squeeze(rx_dataset(2,:,1));
[xc, lags] = xcorr(x1, x0, 80, 'normalized');
stem(ax4, lags, abs(xc), 'filled', 'MarkerSize', 3);
xlabel(ax4, 'Lag (samples)'); ylabel(ax4, '|R_{10}(\tau)|');
title(ax4, 'Cross-correlation elem 2 vs elem 1 (frame 1)'); grid(ax4, 'on');

% --- Conventional Beamformer DOA scan using first code period ----
ax5 = subplot(2,3,6);
code_range = (pad_pre + 1):(pad_pre + code_len*k);
X_code = squeeze(rx_dataset(:, code_range, 1));  % [N_rx x code_len*k]
R_code = X_code * X_code' / numel(code_range);   % Spatial covariance [N_rx x N_rx]

doa_scan = -90:0.5:90;
P_cbf = zeros(1, numel(doa_scan));
for kk = 1:numel(doa_scan)
    a_k       = exp(1j * (0:N_rx-1).' * 2*pi * d_over_lambda * sin(deg2rad(doa_scan(kk))));
    P_cbf(kk) = real(a_k' * R_code * a_k);
end
P_cbf_dB = 10*log10(P_cbf / max(P_cbf));
plot(ax5, doa_scan, P_cbf_dB, 'b');
hold(ax5, 'on');
xline(ax5, doa_deg, 'r--', sprintf('DOA=%.1f°', doa_deg), 'LabelVerticalAlignment','bottom');
xlabel(ax5, 'DOA (°)'); ylabel(ax5, 'Normalised power (dB)');
title(ax5, 'CBF DOA scan (1st code period)');
ylim(ax5, [-40 5]); grid(ax5, 'on');

%% ================================================================
%  Local Functions
%  ================================================================

% GPS L1 C/A Gold code (IS-GPS-200, section 3.3.2.3), BPSK chips (+/-1).
% G1: x^10+x^3+1, G2: x^10+x^9+x^8+x^6+x^3+x^2+1, both initialized to all
% ones; the chip is G1(10) xor the XOR of two PRN-specific G2 stages.
function code = gps_ca_code(prn)
    % G2 phase-select taps per PRN (IS-GPS-200, Table 3-Ia)
    taps = [2  6; 3  7; 4  8; 5  9; 1  9; 2 10; 1  8; 2  9;
            3 10; 2  3; 3  4; 5  6; 6  7; 7  8; 8  9; 9 10;
            1  4; 2  5; 3  6; 4  7; 5  8; 6  9; 1  3; 4  6;
            5  7; 6  8; 7  9; 8 10; 1  6; 2  7; 3  8; 4  9];
    assert(prn >= 1 && prn <= 32, 'PRN number must be in 1..32');

    g1 = ones(1, 10);
    g2 = ones(1, 10);
    code = zeros(1, 1023);
    for i = 1:1023
        chip    = xor(g1(10), xor(g2(taps(prn,1)), g2(taps(prn,2))));
        code(i) = 1 - 2*chip;
        f1 = xor(g1(3), g1(10));
        f2 = mod(g2(2) + g2(3) + g2(6) + g2(8) + g2(9) + g2(10), 2);
        g1 = [f1, g1(1:9)];
        g2 = [f2, g2(1:9)];
    end
end
