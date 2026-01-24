# doa4rfc
#### Realtime Direction-of-Arrival Estimation for RF Communication Protocols

## Objective  
This project aims to provide a flexible software architecture, to implement and test DoA methods for various RF communication protocols.

## Main Features
-  [MultiSync](include/multisync/README.md) for simultaneous processing and phase offset correction with multiple generic frame synchronizers based on [Liquid-DSP](https://liquidsdr.org)
- [multi_rx.h](include/multi_rx/multi_rx.h) for synchronized processing of multiple USRP RX streams in separated threads
- [ZMQ TCP interface](include/zmq_socket/README.md) for forwarding of CFRs to the python app running the DoA algorithm 
- [MATLAB export](include/matlab_export/matlab_export.h) to generate .m files for plotting CFR and constellation diagrams (check [matlabXport](https://github.com/F-L-X-S/matlabXport))
- [MUSIC Algorithm](music/music-spectrum.py) (multiple signal classification) python app based on [pyespargos](https://github.com/ESPARGOS/pyespargos) 
- [Main Application](src/main.cc) to estimate the DoA of an OFDM transmitter using USRPs connected via UHD

## Simulations
 [Simulations](simulations/) provided in ./simulations demonstrate the usage of the provided modules, illustrate the underlying mathematical concepts and show the simulation results:
- [Single-Channel CFR-Estimation](simulations/sim_singlechannel/README.md) 
- [Multi-Channel CFR-Estimation](simulations/sim_multichannel/README.md)
 
## Measurements
[Measurements](measurements/) show the real-world DoA results of the [Application](src/main.cc) using two USRP N210 with WBX daughterboard and provide the corresponding datasets.

## DoA estimation with USRP N210
 The [Main Application](src/main.cc) gives you a functioning example on how to employ the provided modules for DoA estimation with USRP N210. 
![Demo of DoA Estimation](https://github.com/F-L-X-S/doa4rfc/raw/main/docs/assets/doa4rfc.gif)

### Main App Process Flow
```mermaid
---
config:
  look: classic
  layout: elk
  theme: redux
---
flowchart TD
FrameGen["Frame Generator"]
subgraph HardwareInterface["SDR Hardware Interface"]
 subgraph T_StreamWorker["Stream-Worker"]
        UsrpDevices["USRP Device Interface [0..*]"]
        UsrpConf["USRP Interface Setup"]
        StreamConf["Timed Stream Command"]
  end
 subgraph T_RxWorker["RX-Worker [0..*]"]
        RxStream["RX Stream Interface"]
        SampleBlock["Sample Block Buffer"]
  end
 subgraph T_TxWorker["TX-Worker [0..*]"]
        TxStream["TX Stream Interface"]
        TxBuffer["TX Buffer"]
  end
end
 subgraph T_SyncWorker["Sync-Worker"]
        MultiSync["Multi-Channel Synchronization"]
        PhiErrorCorrection["Phase Offset Correction"]
  end
 subgraph T_CfrWorker["CFR-Worker"]
        ZmqSocket["ZMQ socket"]
        MatlabCfrExport["MATLAB Export"]
        FindGroups["Find CFR Groups"]
  end
 subgraph T_CbDataWorker["Callback-Data-Worker"]
        MatlabCbExport["MATLAB Export"]
  end
 subgraph T_TerminalWorker["Terminal-Worker"]
        CheckForPhaseCmd["Check for Phase Adjustment Command"]
        CheckForExitCmd["Check for Exit Command"]
        Exit["Exit Streaming"]
        ReadInput["Read Terminal Inputs"]
  end
    UsrpConf -- Configure Interfaces ---> UsrpDevices
    UsrpDevices -- Provide Device Time --> StreamConf
    StreamConf -- Issue Command  --> UsrpDevices
    UsrpDevices -- Provide Stream Instance ---> RxStream & TxStream
    RxStream -- Forward Samples --> SampleBlock
    RxStream -- Provide Timestamp --> SampleBlock
    SampleBlock -- Push Sample Block --> RxSampleQueue["RX Sample Queue [0..*]"]
    CheckForPhaseCmd -- Push Phase Offset ---> PhiErrorQueue["Phase Offset Queue"]
    PhiErrorQueue -- Provide Phase Offset ---> PhiErrorCorrection
    PhiErrorCorrection -- Adjust Phase ---> MultiSync
    RxSampleQueue -- Provide Sample Blocks ---> MultiSync
    FrameGen -- Write Content ---> TxBuffer
    TxStream -- Transmit Content ---> TxBuffer
    MultiSync -- Push Callback Data ---> CbDataQueue["CB-Data Queue"]
    CbDataQueue -- Provide Callback Data ---> MatlabCbExport
    MultiSync -- Push CFR ---> CfrQueue["CFR Queue"]
    CfrQueue -- Provide CFR ---> FindGroups
    FindGroups -- Provide Group ---> ZmqSocket & MatlabCfrExport
    ReadInput -----> CheckForPhaseCmd & CheckForExitCmd
    CheckForExitCmd -- Triggers ---> Exit

```
### Main App Data-flow
The following diagram illustrates, how samples are streamed from the two SDR-instance, synchronized as sample-blocks with a unique timestamp based on the SDRs device-time and ho the CFRs fro detected frames are grouped and forwarded to the MUISC-algorithm. 
```mermaid
---
config:
  look: classic
  layout: elk
  theme: redux
---
flowchart TD
Sdr1["SDR Channel 1"]
Sdr2["SDR Channel 2"]
subgraph HardwareInterface["SDR Hardware Interface"]
        Rx1["RX  Channel 1"]
        Rx2["RX  Channel 2"]
end

MultiSync["MultiSync"]
Socket["Socket"]

subgraph CbExport["Datasymbol Export"]
    MatlabExportCb["Matlab Export"]
end

subgraph CfrExport["CFR Export"]
    Grouping["Time-based Grouping"]
    MatlabExportCfr["Matlab Export"]
    ZmqExportCfr["Zmq Export"]
end

subgraph DoAAlgorithm["DoA Estimation"]
    ZmqImportCfr["Zmq Import"]
    MusicAlg["MUISC Algorithm"]
end

Sdr1 -- Sample-Stream ---> Rx1
Sdr2 -- Sample-Stream ---> Rx2

Sdr1 -- Timestamp ---> Rx1
Sdr2 -- Timestamp ---> Rx2

Rx1 -- Sample-Block & Timestamp ---> MultiSync
Rx2 -- Sample-Block & Timestamp ---> MultiSync

MultiSync -- Datasymbols & Timestamp ---> MatlabExportCb
MultiSync -- CFR & Timestamp ---> Grouping
Grouping -- CFR-Group & Timestamp ---> MatlabExportCfr
Grouping -- CFR-Group ---> ZmqExportCfr
ZmqExportCfr  -- CFR-Group ---> Socket
Socket -- CFR-Groups [1..*] --->ZmqImportCfr
ZmqImportCfr -- CFR-Groups [1..*] ---> MusicAlg

```

### Hardware Setup 
The software is tested using two USRP N210 with the WBXv3 daughterboard. Phase synchronization is achieved with the MIMO-cable. The USRPs are connected to the host by separate ethernet interfaces. For utilizing a different type of SDRs, the interfaces can be implemented in separated threads similar to `multi_rx.h`.  <br>
One USRP is used for transmitting and receiving the OFDM packages while the other USRP is used in RX-mode only. The MUSIC-spectrum visualizes the position of the TX-antenna. 
 <br>
Make sure, the receiving antennas are spaced by the half wavelength of the carrier frequency (e.g. 12cm for a carrier of 1.25GHz).

### Installation 
1. Clone the Repo to your local machine
2. Setup a virtual environment within the `./music/`directory <br>
   ```
   cd ./music
   python -m venv env
   ```
3. Install all python dependencies specified in `requirements.txt` <br>
   ```
    source env/bin/activate
    pip install -e . 
   ``` 
4. Use the CMake extension to configure the project 
5. Set `doa4rfc` as target for build and execution (or any sim-file)
6. Go to the vscode "run and debug" menu and start the `Debug (Clang CMake Preset)` task to build and run the specified target 
<br><br>

Make sure, that all USRPs are connected via separate Ethernet interfaces, since the datarate can possibly cause overflows in the shared-Etehrnet mode. Check the USRP connection by running `uhd_find_devices`. 

#### How to install the doa4rfc Gnuradio-OOT module 
 1. `cd` to `gr-doa4rfc/build` (create, if not existing) <br>
 2. Run `cmake -DCMAKE_INSTALL_PREFIX=/opt/homebrew ../` (replace with your grc installation path)<br>
 3. Run `make`<br>
 4. `sudo make install`<br>
 5. Open gnuradio-companion and refresh your blocks. A new doa4rfc section should appear below the core module. 

### Main Dependencies
- [ZMQ](https://zeromq.org/languages/cplusplus/) for socket communication with the Python-implemented DoA Algorithm 
- [Liquid-DSP](https://liquidsdr.org) for frame-detection, generation and synchronization
- [UHD](https://files.ettus.com/manual/index.html) for USRP communication
