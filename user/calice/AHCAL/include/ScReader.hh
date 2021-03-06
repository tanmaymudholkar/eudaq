#ifndef SCREADER_HH
#define SCREADER_HH

#include "AHCALProducer.hh"
#include <deque>

namespace eudaq {
  
  class ScReader : public AHCALReader {
  public:
    virtual void Read(std::deque<char> & buf, std::deque<eudaq::RawDataEvent *> & deqEvent);
    virtual void OnStart(int runNo);
    virtual void OnStop(int waitQueueTimeS);
    virtual void OnConfigLED(std::string _fname);//chose configuration file for LED runs

    virtual std::deque<eudaq::RawDataEvent *> NewEvent_createRawDataEvent(std::deque<eudaq::RawDataEvent *>  deqEvent, bool tempcome, int LdaRawcycle, bool newForced);
    virtual void readTemperature(std::deque<char> buf);
    virtual void AppendBlockTemperature(std::deque<eudaq::RawDataEvent *> deqEvent, int nb);
    virtual void AppendBlockGeneric(std::deque<eudaq::RawDataEvent *> deqEvent, int nb, std::vector<int> intVector);
    virtual void AppendBlockGeneric_32(std::deque<eudaq::RawDataEvent *> deqEvent, int nb, std::vector<uint32_t> intVector);

    virtual void readSpirocData_AddBlock(std::deque<char> buf, std::deque<eudaq::RawDataEvent *> deqEvent);
    
    ScReader(AHCALProducer *r) : AHCALReader(r) {_runNo = -1;}
    ~ScReader(){}
    
  private:
    enum {e_sizeLdaHeader = 10}; // 8bytes + 0xcdcd
    int _runNo;
    unsigned int _cycleNo;
    unsigned int _trigID;
    unsigned int length;

    bool _tempmode; // during the temperature readout time
    bool _buffer_inside_acquisition;//the reader is reading data from withing the acquisition, defined by start and stop commands
    //uint64_t _last_stop_ts; //timestamp of the last stop of acquisition

    std::vector< std::pair<std::pair<int,int>, int> > _vecTemp; // (lda, port), data;
    std::vector<int> slowcontrol;
    std::vector<int> ledInfo;
    std::vector<uint32_t> cycleData;
   
  };
}

#endif // CALICEPRODUCER_HH

