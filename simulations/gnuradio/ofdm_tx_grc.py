#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
# SPDX-License-Identifier: GPL-3.0
#
# GNU Radio Python Flow Graph
# Title: Not titled yet
# GNU Radio version: 3.10.12.0

from PyQt5 import Qt
from gnuradio import qtgui
from PyQt5 import QtCore
from gnuradio import analog
from gnuradio import blocks
from gnuradio import digital
from gnuradio import filter
from gnuradio.filter import firdes
from gnuradio import gr
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
import doa4rfc_zmq_if_sink
import sip
import threading



class ofdm_tx_grc(gr.top_block, Qt.QWidget):

    def __init__(self):
        gr.top_block.__init__(self, "Not titled yet", catch_exceptions=True)
        Qt.QWidget.__init__(self)
        self.setWindowTitle("Not titled yet")
        qtgui.util.check_set_qss()
        try:
            self.setWindowIcon(Qt.QIcon.fromTheme('gnuradio-grc'))
        except BaseException as exc:
            print(f"Qt GUI: Could not set Icon: {str(exc)}", file=sys.stderr)
        self.top_scroll_layout = Qt.QVBoxLayout()
        self.setLayout(self.top_scroll_layout)
        self.top_scroll = Qt.QScrollArea()
        self.top_scroll.setFrameStyle(Qt.QFrame.NoFrame)
        self.top_scroll_layout.addWidget(self.top_scroll)
        self.top_scroll.setWidgetResizable(True)
        self.top_widget = Qt.QWidget()
        self.top_scroll.setWidget(self.top_widget)
        self.top_layout = Qt.QVBoxLayout(self.top_widget)
        self.top_grid_layout = Qt.QGridLayout()
        self.top_layout.addLayout(self.top_grid_layout)

        self.settings = Qt.QSettings("gnuradio/flowgraphs", "ofdm_tx_grc")

        try:
            geometry = self.settings.value("geometry")
            if geometry:
                self.restoreGeometry(geometry)
        except BaseException as exc:
            print(f"Qt GUI: Could not restore geometry: {str(exc)}", file=sys.stderr)
        self.flowgraph_started = threading.Event()

        ##################################################
        # Variables
        ##################################################
        self.f_s = f_s = 64
        self.diff_group_delay = diff_group_delay = (-1)
        self.base_group_delay = base_group_delay = 10
        self.sym_rate = sym_rate = f_s
        self.samp_rate = samp_rate = 32000
        self.rrc_taps_ch3 = rrc_taps_ch3 = 2*(base_group_delay+3*diff_group_delay)+1
        self.rrc_taps_ch2 = rrc_taps_ch2 = 2*(base_group_delay+2*diff_group_delay)+1
        self.rrc_taps_ch1 = rrc_taps_ch1 = 2*(base_group_delay+1*diff_group_delay)+1
        self.rrc_taps_ch0 = rrc_taps_ch0 = (2*base_group_delay)+1
        self.packet_len = packet_len = 64
        self.noise_pad_factor = noise_pad_factor = 2
        self.interp_factor = interp_factor = 2
        self.f_c = f_c = 1206

        ##################################################
        # Blocks
        ##################################################

        self.root_raised_cosine_filter_0_1_0 = filter.fir_filter_ccf(
            interp_factor,
            firdes.root_raised_cosine(
                1,
                f_s,
                sym_rate,
                0.01,
                (rrc_taps_ch3*f_s)))
        self.root_raised_cosine_filter_0_1 = filter.fir_filter_ccf(
            interp_factor,
            firdes.root_raised_cosine(
                1,
                f_s,
                sym_rate,
                0.01,
                (rrc_taps_ch2*f_s)))
        self.root_raised_cosine_filter_0_0 = filter.fir_filter_ccf(
            interp_factor,
            firdes.root_raised_cosine(
                1,
                f_s,
                sym_rate,
                0.01,
                (rrc_taps_ch1*f_s)))
        self.root_raised_cosine_filter_0 = filter.fir_filter_ccf(
            interp_factor,
            firdes.root_raised_cosine(
                1,
                f_s,
                sym_rate,
                0.01,
                (rrc_taps_ch0*f_s)))
        self.qtgui_freq_sink_x_0 = qtgui.freq_sink_c(
            1024, #size
            window.WIN_BLACKMAN_hARRIS, #wintype
            0, #fc
            samp_rate, #bw
            "", #name
            1,
            None # parent
        )
        self.qtgui_freq_sink_x_0.set_update_time(0.10)
        self.qtgui_freq_sink_x_0.set_y_axis((-140), 10)
        self.qtgui_freq_sink_x_0.set_y_label('Relative Gain', 'dB')
        self.qtgui_freq_sink_x_0.set_trigger_mode(qtgui.TRIG_MODE_FREE, 0.0, 0, "")
        self.qtgui_freq_sink_x_0.enable_autoscale(False)
        self.qtgui_freq_sink_x_0.enable_grid(False)
        self.qtgui_freq_sink_x_0.set_fft_average(1.0)
        self.qtgui_freq_sink_x_0.enable_axis_labels(True)
        self.qtgui_freq_sink_x_0.enable_control_panel(False)
        self.qtgui_freq_sink_x_0.set_fft_window_normalized(False)



        labels = ['', '', '', '', '',
            '', '', '', '', '']
        widths = [1, 1, 1, 1, 1,
            1, 1, 1, 1, 1]
        colors = ["blue", "red", "green", "black", "cyan",
            "magenta", "yellow", "dark red", "dark green", "dark blue"]
        alphas = [1.0, 1.0, 1.0, 1.0, 1.0,
            1.0, 1.0, 1.0, 1.0, 1.0]

        for i in range(1):
            if len(labels[i]) == 0:
                self.qtgui_freq_sink_x_0.set_line_label(i, "Data {0}".format(i))
            else:
                self.qtgui_freq_sink_x_0.set_line_label(i, labels[i])
            self.qtgui_freq_sink_x_0.set_line_width(i, widths[i])
            self.qtgui_freq_sink_x_0.set_line_color(i, colors[i])
            self.qtgui_freq_sink_x_0.set_line_alpha(i, alphas[i])

        self._qtgui_freq_sink_x_0_win = sip.wrapinstance(self.qtgui_freq_sink_x_0.qwidget(), Qt.QWidget)
        self.top_layout.addWidget(self._qtgui_freq_sink_x_0_win)
        self.interp_fir_filter_xxx_0_2 = filter.interp_fir_filter_ccc(interp_factor, )
        self.interp_fir_filter_xxx_0_2.declare_sample_delay(1)
        self.interp_fir_filter_xxx_0_1 = filter.interp_fir_filter_ccc(interp_factor, )
        self.interp_fir_filter_xxx_0_1.declare_sample_delay(1)
        self.interp_fir_filter_xxx_0_0 = filter.interp_fir_filter_ccc(interp_factor, )
        self.interp_fir_filter_xxx_0_0.declare_sample_delay(1)
        self.interp_fir_filter_xxx_0 = filter.interp_fir_filter_ccc(interp_factor, )
        self.interp_fir_filter_xxx_0.declare_sample_delay(1)
        self.doa4rfc_zmq_if_sink_0 = doa4rfc_zmq_if_sink.zmq_if_sink('tcp://127.0.0.1:5554', 4)
        self.digital_ofdm_tx_0 = digital.ofdm_tx(
            fft_len=f_s,
            cp_len=16,
            packet_length_tag_key='packet_len',
            occupied_carriers=[list(range(-26, -21)) + list(range(-20, -7)) + list(range(-6, 0)) + list(range(1, 7)) + list(range(8, 21)) + list(range(22, 27))],
            pilot_carriers=((-21, -7, 7, 21,),),
            pilot_symbols=tuple((x, x, x, -x) for x in (1, 1, 1, 1, -1, -1, -1, 1, -1, -1, -1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1, 1, 1, 1, 1, 1, -1, 1,
        	1, 1, -1, 1, 1, -1, -1, 1, 1, 1, -1, 1, -1, -1, -1, 1, -1, 1, -1, -1, 1, -1, -1, 1, 1, 1, 1, 1, -1, -1, 1, 1,
        	-1, -1, 1, -1, 1, -1, 1, 1, -1, -1, -1, 1, 1, -1, -1, -1, -1, 1, -1, -1, 1, -1, 1, 1, 1, 1, -1, 1, -1, 1, -1, 1,
        	-1, -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1, 1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1)),
            sync_word1=[0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, -1.4142135623730951, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, -1.4142135623730951, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, -1.4142135623730951, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, 1.4142135623730951, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
        ,
            sync_word2=[0, 0, 0, 0, 0, 0, 1, 1, -1, 1, -1, -1, -1, 1, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, 1, 1, -1, -1, -1, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, -1, -1, -1, 1, -1, -1, 1, -1, 1, -1, 1, -1, -1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0]
        ,
            bps_header=2,
            bps_payload=2,
            rolloff=0,
            debug_log=False,
            scramble_bits=False)
        self._diff_group_delay_range = qtgui.Range((-3), 3, 1, (-1), 10)
        self._diff_group_delay_win = qtgui.RangeWidget(self._diff_group_delay_range, self.set_diff_group_delay, "'diff_group_delay'", "counter_slider", int, QtCore.Qt.Horizontal)
        self.top_layout.addWidget(self._diff_group_delay_win)
        self.blocks_vector_source_x_0 = blocks.vector_source_b(range(0,50), True, 1, [])
        self.blocks_throttle2_0 = blocks.throttle( gr.sizeof_gr_complex*1, samp_rate, True, 0 if "auto" == "auto" else max( int(float(0.1) * samp_rate) if "auto" == "time" else int(0.1), 1) )
        self.blocks_stream_to_tagged_stream_0 = blocks.stream_to_tagged_stream(gr.sizeof_char, 1, 50, "packet_len")
        self.blocks_stream_mux_0 = blocks.stream_mux(gr.sizeof_gr_complex*1, (packet_len*f_s, noise_pad_factor*packet_len*f_s))
        self.blocks_null_source_0 = blocks.null_source(gr.sizeof_gr_complex*1)
        self.blocks_multiply_xx_0 = blocks.multiply_vcc(1)
        self.blocks_multiply_conjugate_cc_0_2 = blocks.multiply_conjugate_cc(1)
        self.blocks_multiply_conjugate_cc_0_1 = blocks.multiply_conjugate_cc(1)
        self.blocks_multiply_conjugate_cc_0_0 = blocks.multiply_conjugate_cc(1)
        self.blocks_multiply_conjugate_cc_0 = blocks.multiply_conjugate_cc(1)
        self.blocks_add_xx_0_0_0_0 = blocks.add_vcc(1)
        self.blocks_add_xx_0_0_0 = blocks.add_vcc(1)
        self.blocks_add_xx_0_0 = blocks.add_vcc(1)
        self.blocks_add_xx_0 = blocks.add_vcc(1)
        self._base_group_delay_range = qtgui.Range(0, 25, 1, 10, 10)
        self._base_group_delay_win = qtgui.RangeWidget(self._base_group_delay_range, self.set_base_group_delay, "'base_group_delay'", "counter_slider", int, QtCore.Qt.Horizontal)
        self.top_layout.addWidget(self._base_group_delay_win)
        self.analog_sig_source_x_0 = analog.sig_source_c(f_s, analog.GR_COS_WAVE, f_c, 1, 0, 0)
        self.analog_noise_source_x_0_1_0 = analog.noise_source_c(analog.GR_GAUSSIAN, 1, 0)
        self.analog_noise_source_x_0_1 = analog.noise_source_c(analog.GR_GAUSSIAN, 1, 1)
        self.analog_noise_source_x_0_0 = analog.noise_source_c(analog.GR_GAUSSIAN, 1, 3)
        self.analog_noise_source_x_0 = analog.noise_source_c(analog.GR_GAUSSIAN, 1, 2)


        ##################################################
        # Connections
        ##################################################
        self.connect((self.analog_noise_source_x_0, 0), (self.blocks_add_xx_0_0_0, 1))
        self.connect((self.analog_noise_source_x_0_0, 0), (self.blocks_add_xx_0_0_0_0, 1))
        self.connect((self.analog_noise_source_x_0_1, 0), (self.blocks_add_xx_0_0, 1))
        self.connect((self.analog_noise_source_x_0_1_0, 0), (self.blocks_add_xx_0, 1))
        self.connect((self.analog_sig_source_x_0, 0), (self.blocks_multiply_conjugate_cc_0, 1))
        self.connect((self.analog_sig_source_x_0, 0), (self.blocks_multiply_conjugate_cc_0_0, 1))
        self.connect((self.analog_sig_source_x_0, 0), (self.blocks_multiply_conjugate_cc_0_1, 1))
        self.connect((self.analog_sig_source_x_0, 0), (self.blocks_multiply_conjugate_cc_0_2, 1))
        self.connect((self.analog_sig_source_x_0, 0), (self.blocks_multiply_xx_0, 0))
        self.connect((self.blocks_add_xx_0, 0), (self.interp_fir_filter_xxx_0, 0))
        self.connect((self.blocks_add_xx_0_0, 0), (self.interp_fir_filter_xxx_0_0, 0))
        self.connect((self.blocks_add_xx_0_0_0, 0), (self.interp_fir_filter_xxx_0_2, 0))
        self.connect((self.blocks_add_xx_0_0_0_0, 0), (self.interp_fir_filter_xxx_0_1, 0))
        self.connect((self.blocks_multiply_conjugate_cc_0, 0), (self.doa4rfc_zmq_if_sink_0, 0))
        self.connect((self.blocks_multiply_conjugate_cc_0_0, 0), (self.doa4rfc_zmq_if_sink_0, 1))
        self.connect((self.blocks_multiply_conjugate_cc_0_1, 0), (self.doa4rfc_zmq_if_sink_0, 3))
        self.connect((self.blocks_multiply_conjugate_cc_0_2, 0), (self.doa4rfc_zmq_if_sink_0, 2))
        self.connect((self.blocks_multiply_xx_0, 0), (self.blocks_throttle2_0, 0))
        self.connect((self.blocks_null_source_0, 0), (self.blocks_stream_mux_0, 1))
        self.connect((self.blocks_stream_mux_0, 0), (self.blocks_multiply_xx_0, 1))
        self.connect((self.blocks_stream_to_tagged_stream_0, 0), (self.digital_ofdm_tx_0, 0))
        self.connect((self.blocks_throttle2_0, 0), (self.blocks_add_xx_0, 0))
        self.connect((self.blocks_throttle2_0, 0), (self.blocks_add_xx_0_0, 0))
        self.connect((self.blocks_throttle2_0, 0), (self.blocks_add_xx_0_0_0, 0))
        self.connect((self.blocks_throttle2_0, 0), (self.blocks_add_xx_0_0_0_0, 0))
        self.connect((self.blocks_vector_source_x_0, 0), (self.blocks_stream_to_tagged_stream_0, 0))
        self.connect((self.digital_ofdm_tx_0, 0), (self.blocks_stream_mux_0, 0))
        self.connect((self.interp_fir_filter_xxx_0, 0), (self.root_raised_cosine_filter_0, 0))
        self.connect((self.interp_fir_filter_xxx_0_0, 0), (self.root_raised_cosine_filter_0_0, 0))
        self.connect((self.interp_fir_filter_xxx_0_1, 0), (self.root_raised_cosine_filter_0_1_0, 0))
        self.connect((self.interp_fir_filter_xxx_0_2, 0), (self.root_raised_cosine_filter_0_1, 0))
        self.connect((self.root_raised_cosine_filter_0, 0), (self.blocks_multiply_conjugate_cc_0, 0))
        self.connect((self.root_raised_cosine_filter_0_0, 0), (self.blocks_multiply_conjugate_cc_0_0, 0))
        self.connect((self.root_raised_cosine_filter_0_1, 0), (self.blocks_multiply_conjugate_cc_0_2, 0))
        self.connect((self.root_raised_cosine_filter_0_1_0, 0), (self.blocks_multiply_conjugate_cc_0_1, 0))
        self.connect((self.root_raised_cosine_filter_0_1_0, 0), (self.qtgui_freq_sink_x_0, 0))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("gnuradio/flowgraphs", "ofdm_tx_grc")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_f_s(self):
        return self.f_s

    def set_f_s(self, f_s):
        self.f_s = f_s
        self.set_sym_rate(self.f_s)
        self.analog_sig_source_x_0.set_sampling_freq(self.f_s)
        self.root_raised_cosine_filter_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch0*self.f_s)))
        self.root_raised_cosine_filter_0_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch1*self.f_s)))
        self.root_raised_cosine_filter_0_1.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch2*self.f_s)))
        self.root_raised_cosine_filter_0_1_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch3*self.f_s)))

    def get_diff_group_delay(self):
        return self.diff_group_delay

    def set_diff_group_delay(self, diff_group_delay):
        self.diff_group_delay = diff_group_delay
        self.set_rrc_taps_ch1(2*(self.base_group_delay+1*self.diff_group_delay)+1)
        self.set_rrc_taps_ch2(2*(self.base_group_delay+2*self.diff_group_delay)+1)
        self.set_rrc_taps_ch3(2*(self.base_group_delay+3*self.diff_group_delay)+1)

    def get_base_group_delay(self):
        return self.base_group_delay

    def set_base_group_delay(self, base_group_delay):
        self.base_group_delay = base_group_delay
        self.set_rrc_taps_ch0((2*self.base_group_delay)+1)
        self.set_rrc_taps_ch1(2*(self.base_group_delay+1*self.diff_group_delay)+1)
        self.set_rrc_taps_ch2(2*(self.base_group_delay+2*self.diff_group_delay)+1)
        self.set_rrc_taps_ch3(2*(self.base_group_delay+3*self.diff_group_delay)+1)

    def get_sym_rate(self):
        return self.sym_rate

    def set_sym_rate(self, sym_rate):
        self.sym_rate = sym_rate
        self.root_raised_cosine_filter_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch0*self.f_s)))
        self.root_raised_cosine_filter_0_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch1*self.f_s)))
        self.root_raised_cosine_filter_0_1.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch2*self.f_s)))
        self.root_raised_cosine_filter_0_1_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch3*self.f_s)))

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.blocks_throttle2_0.set_sample_rate(self.samp_rate)
        self.qtgui_freq_sink_x_0.set_frequency_range(0, self.samp_rate)

    def get_rrc_taps_ch3(self):
        return self.rrc_taps_ch3

    def set_rrc_taps_ch3(self, rrc_taps_ch3):
        self.rrc_taps_ch3 = rrc_taps_ch3
        self.root_raised_cosine_filter_0_1_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch3*self.f_s)))

    def get_rrc_taps_ch2(self):
        return self.rrc_taps_ch2

    def set_rrc_taps_ch2(self, rrc_taps_ch2):
        self.rrc_taps_ch2 = rrc_taps_ch2
        self.root_raised_cosine_filter_0_1.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch2*self.f_s)))

    def get_rrc_taps_ch1(self):
        return self.rrc_taps_ch1

    def set_rrc_taps_ch1(self, rrc_taps_ch1):
        self.rrc_taps_ch1 = rrc_taps_ch1
        self.root_raised_cosine_filter_0_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch1*self.f_s)))

    def get_rrc_taps_ch0(self):
        return self.rrc_taps_ch0

    def set_rrc_taps_ch0(self, rrc_taps_ch0):
        self.rrc_taps_ch0 = rrc_taps_ch0
        self.root_raised_cosine_filter_0.set_taps(firdes.root_raised_cosine(1, self.f_s, self.sym_rate, 0.01, (self.rrc_taps_ch0*self.f_s)))

    def get_packet_len(self):
        return self.packet_len

    def set_packet_len(self, packet_len):
        self.packet_len = packet_len

    def get_noise_pad_factor(self):
        return self.noise_pad_factor

    def set_noise_pad_factor(self, noise_pad_factor):
        self.noise_pad_factor = noise_pad_factor

    def get_interp_factor(self):
        return self.interp_factor

    def set_interp_factor(self, interp_factor):
        self.interp_factor = interp_factor

    def get_f_c(self):
        return self.f_c

    def set_f_c(self, f_c):
        self.f_c = f_c
        self.analog_sig_source_x_0.set_frequency(self.f_c)




def main(top_block_cls=ofdm_tx_grc, options=None):

    qapp = Qt.QApplication(sys.argv)

    tb = top_block_cls()

    tb.start()
    tb.flowgraph_started.set()

    tb.show()

    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()

        Qt.QApplication.quit()

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    timer = Qt.QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    qapp.exec_()

if __name__ == '__main__':
    main()
