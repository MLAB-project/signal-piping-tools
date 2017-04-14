# signal-piping-tools

A set of utilities for handling signal data streams from SDR receivers.  These utilities are used mainly at radio detection stations in measuring networks. 

## Installation

Get dependencies:

    $ sudo apt-get install build-essential  libusb-dev libusb-1.0-0-dev  python-setuptools  libcfitsio-dev buffer pv ntp libvolk1-dev

And compile:

    $ git clone https://github.com/MLAB-project/signal-piping-tools.git
    $ cd signal-piping-tools
    $ make
