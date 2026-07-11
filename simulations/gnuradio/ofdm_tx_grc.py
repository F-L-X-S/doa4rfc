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
from gnuradio import blocks
from gnuradio import digital
from gnuradio import gr
from gnuradio.filter import firdes
from gnuradio.fft import window
import sys
import signal
from PyQt5 import Qt
from argparse import ArgumentParser
from gnuradio.eng_arg import eng_float, intx
from gnuradio import eng_notation
import doa4rfc_zmq_if_sink
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
        self.samp_rate = samp_rate = 32000
        self.packet_len = packet_len = 64

        ##################################################
        # Blocks
        ##################################################

        self.doa4rfc_zmq_if_sink_0 = doa4rfc_zmq_if_sink.zmq_if_sink('tcp://127.0.0.1:5554', 4)
        self.digital_ofdm_tx_0 = digital.ofdm_tx(
            fft_len=64,
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
            bps_payload=3,
            rolloff=0,
            debug_log=False,
            scramble_bits=False)
        self.blocks_vector_source_x_0 = blocks.vector_source_b(range(0,50), True, 1, [])
        self.blocks_stream_to_tagged_stream_0 = blocks.stream_to_tagged_stream(gr.sizeof_char, 1, 50, "packet_len")


        ##################################################
        # Connections
        ##################################################
        self.connect((self.blocks_stream_to_tagged_stream_0, 0), (self.digital_ofdm_tx_0, 0))
        self.connect((self.blocks_vector_source_x_0, 0), (self.blocks_stream_to_tagged_stream_0, 0))
        self.connect((self.digital_ofdm_tx_0, 0), (self.doa4rfc_zmq_if_sink_0, 0))
        self.connect((self.digital_ofdm_tx_0, 0), (self.doa4rfc_zmq_if_sink_0, 1))
        self.connect((self.digital_ofdm_tx_0, 0), (self.doa4rfc_zmq_if_sink_0, 2))
        self.connect((self.digital_ofdm_tx_0, 0), (self.doa4rfc_zmq_if_sink_0, 3))


    def closeEvent(self, event):
        self.settings = Qt.QSettings("gnuradio/flowgraphs", "ofdm_tx_grc")
        self.settings.setValue("geometry", self.saveGeometry())
        self.stop()
        self.wait()

        event.accept()

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate

    def get_packet_len(self):
        return self.packet_len

    def set_packet_len(self, packet_len):
        self.packet_len = packet_len




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
