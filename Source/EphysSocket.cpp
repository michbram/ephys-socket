#ifdef _WIN32
#include <Windows.h>
#endif

#include "EphysSocket.h"
#include "EphysSocketEditor.h"

using namespace EphysSocketNode;

DataThread *EphysSocket::createDataThread(SourceNode *sn)
{
    return new EphysSocket(sn);
}

EphysSocket::EphysSocket(SourceNode *sn) : DataThread(sn),
                                           ipaddr(""),
                                           port(DEFAULT_PORT),
                                           num_channels(DEFAULT_NUM_CHANNELS),
                                           num_samp(DEFAULT_NUM_SAMPLES),
                                           data_offset(DEFAULT_DATA_OFFSET),
                                           data_scale(DEFAULT_DATA_SCALE),
                                           sample_rate(DEFAULT_SAMPLE_RATE)
{
    socket = new DatagramSocket();
    socket->bindToPort(port, "192.168.137.1");
    connected = (socket->waitUntilReady(true, 500) == 1);   // Try to automatically open, dont worry if it does not work
    sourceBuffers.add(new DataBuffer(num_channels, 10000)); // start with 2 channels and automatically resize
    recvbuf = (uint16_t *)malloc(num_channels * num_samp * 2);
    convbuf = (float *)malloc(num_channels * num_samp * 40);
}

GenericEditor *EphysSocket::createEditor(SourceNode *sn)
{
    return new EphysSocketEditor(sn, this);
}

EphysSocket::~EphysSocket()
{
    free(recvbuf);
    free(convbuf);
}

void EphysSocket::resizeChanSamp()
{
    sourceBuffers[0]->resize(num_channels, 100000);
    recvbuf = (uint16_t *)realloc(recvbuf, num_channels * num_samp * 2);
    convbuf = (float *)realloc(convbuf, num_channels * num_samp * 40);
    timestamps.resize(num_samp);
    ttlEventWords.resize(num_samp);
}

int EphysSocket::getNumChannels() const
{
    return num_channels;
}

int EphysSocket::getNumDataOutputs(DataChannel::DataChannelTypes type, int subproc) const
{
    if (type == DataChannel::HEADSTAGE_CHANNEL)
        return num_channels;
    else
        return 0;
}

int EphysSocket::getNumTTLOutputs(int subproc) const
{
    return 0;
}

float EphysSocket::getSampleRate(int subproc) const
{
    return sample_rate;
}

float EphysSocket::getBitVolts(const DataChannel *ch) const
{
    return data_scale;
}

bool EphysSocket::foundInputSource()
{
    return true;
}

bool EphysSocket::startAcquisition()
{
    resizeChanSamp();
    std::cout << "sampChan resized" << std::endl;

    total_samples = 0;
    first = true;

    if (writeCommand(1))
    {
        // Time::waitForMillisecondCounter(Time::getMillisecondCounter() + 1000);
        tryToConnect();
        startThread();
        // startTimer(5000);
        return true;
    }
    else
    {
        return false;
    }
}

bool EphysSocket::writeCommand(int command)
{
    uint8_t debug = 1;
    uint8_t freqIdx = (int)(sample_rate/1000);
    uint8_t commands[67];
    int numToWrite;
    if (command)
    {
        commands[0] = 1 << 6;
        commands[1] = debug;
        commands[2] = freqIdx;

        for (uint8_t x = 0; x < 32; x++)
        {
            commands[x * 2 + 3] = 0x0 | x;
            commands[x * 2 + 4] = 0;
        }
        numToWrite = 67;
    }
    else
    {
        commands[0] = 1 << 7;
        commands[1] = debug;
        commands[2] = freqIdx;
        numToWrite = 3;
    }

    std::cout << "Waiting to write" << std::endl;
    DatagramSocket sendSocket = new DatagramSocket();
    int ready = sendSocket.waitUntilReady(false, 1000);
    if (ready == 1)
    {
        sendSocket.write(ipaddr, 3333, &commands[0], numToWrite);
        return true;
    }
    else
    {
        std::cout << "Socket is not ready to write. Err : " << ready << std::endl;
        return false;
    }
}

bool EphysSocket::tryToConnect()
{
    socket->shutdown();
    socket = new DatagramSocket();
    bool bound = socket->bindToPort(port);
    if (bound)
    {
        std::cout << "Socket bound to port " << port << std::endl;
        connected = (socket->waitUntilReady(true, 500) == 1);
    }
    else
    {
        std::cout << "Could not bind socket to port " << port << std::endl;
        return false;
    }

    if (connected)
    {
        std::cout << "Socket connected." << std::endl;
        return true;
    }
    else
    {
        std::cout << "Socket failed to connect" << std::endl;
        return false;
    }
}

bool EphysSocket::stopAcquisition()
{
    // stopTimer();
    if (writeCommand(0))
    {
        std::cout << "Stop command sent" << std::endl;
        if (isThreadRunning())
        {
            signalThreadShouldExit();
        }

        waitForThreadToExit(500);
        sourceBuffers[0]->clear();

        return true;
    }
    else
    {
        CoreServices::sendStatusMessage("Ephys Socket: Cannot send stop signal");
        return false;
    }
}

bool EphysSocket::updateBuffer()
{
    String ipAddFrom;
    int portFrom;
    int64 lastTimestamp = 0;

    int rc = socket->read(recvbuf, num_channels * num_samp * 2, true, ipAddFrom, portFrom);

    if (ipAddFrom != ipaddr)
    {
        std::cout << "Not the same address" << std::endl;
    }
    else
    {
        if (rc == -1)
        {
            CoreServices::sendStatusMessage("Ephys Socket: Data shape mismatch");
            return false;
        }

        for (int i = 0; i < num_samp * num_channels; i++)
        {
            // convbuf[i] = data_scale * (float)(recvbuf[i] - data_offset);
            convbuf[i] = (float)(recvbuf[i] - data_offset);
            timestamps.set(i, total_samples + i);
        }

        sourceBuffers[0]->addToBuffer(convbuf,
                                    timestamps.getRawDataPointer(),
                                    ttlEventWords.getRawDataPointer(),
                                    num_samp,
                                    1);

        total_samples += num_samp;
    }
    return true;
}

void EphysSocket::timerCallback()
{
    // std::cout << "Expected samples: " << int(sample_rate * 5) << ", Actual samples: " << total_samples << std::endl;

    relative_sample_rate = (sample_rate * 5) / float(total_samples);

    // total_samples = 0;
}
