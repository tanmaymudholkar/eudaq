#include "eudaq/Monitor.hh"
#include "eudaq/Logger.hh"

#define EUDAQ_MAX_EVENTS_PER_IDLE 1000

namespace eudaq {

  Monitor::Monitor(const std::string &name, const std::string &runcontrol,
                   const unsigned lim, const unsigned skip_,
                   const unsigned int skip_evts, const std::string &datafile)
      : CommandReceiver("Monitor", name, runcontrol),
        m_callstart(false), m_reader(0), limit(lim), skip(100 - skip_),
        skip_events_with_counter(skip_evts),m_done(false) {
    if (datafile != ""){
      m_reader = Factory<FileReader>::MakeShared(cstr2hash("RawFileReader"), datafile);
      std::cout << "DEBUG: Reading file " << datafile << " -> "
                << m_reader->Filename() << std::endl;
    }
  }

  bool Monitor::ProcessEvent() { //TODO:: Deal with BORE
    if (!m_reader)
      return false;
    if (!m_reader->NextEvent())
      return false;

    unsigned evt_number = m_reader->GetNextEvent()->GetEventNumber();
    if (limit > 0 && evt_number > limit)
      return true;

    if (evt_number % 1000 == 0) {
      std::cout << "ProcessEvent "
                << m_reader->GetNextEvent()->GetEventNumber()
                << (m_reader->GetNextEvent()->IsBORE()
                        ? "B"
                        : m_reader->GetNextEvent()->IsEORE() ? "E" : "")
                << std::endl;
    }

    if (skip > 0 && (evt_number % 100 >= skip)) { //-s functionality
      return true;
    } else if (skip_events_with_counter >
               0) { //-sc functionality, you cant have both
      if (++counter_for_skipping < skip_events_with_counter && evt_number > 0 &&
          !m_reader->GetNextEvent()->IsBORE() &&
          !m_reader->GetNextEvent()->IsEORE())
        return true;
      else
        counter_for_skipping = 0;
    }

    try {
      OnEvent(m_reader->GetNextEvent());
    } catch (const InterruptedException &) {
      return false;
    }
    return true;
  }

  void Monitor::OnIdle() {
    if (m_callstart) {
      m_callstart = false;
      OnStartRun();
    }
    bool processed = false;
    for (int i = 0; i < EUDAQ_MAX_EVENTS_PER_IDLE; ++i) {
      if (ProcessEvent()) {
        processed = true;
      } else {
        break;
      }
    }
    if (!processed)
      mSleep(1);
  }

  void Monitor::OnStartRun() {
    try{  
      uint32_t run = GetRunNumber();
      std::cout << "run " << run << std::endl;
      m_reader = Factory<FileReader>::MakeShared(str2hash("RawFileReader"), to_string(run));
      EUDAQ_INFO("Starting run " + to_string(run));
      SetStatus(Status::STATE_RUNNING, "Started");
    } catch (const Exception &e) {
      std::string msg = "Error preparing for run " + std::to_string(GetRunNumber()) + ": " + e.what();
      EUDAQ_ERROR(msg);
      SetStatus(Status::STATE_ERROR, msg);
    }
  }

  void Monitor::OnStopRun() {
    try{  
      m_reader->Interrupt();
      SetStatus(Status::STATE_CONF, "Stopped");
    } catch (const Exception &e) {
      std::string msg = "Error stopping for run " + std::to_string(GetRunNumber()) + ": " + e.what();
      EUDAQ_ERROR(msg);
      SetStatus(Status::STATE_ERROR, msg);
    }
  }

  void Monitor::Exec(){
    // try {
    //   while (!m_done){
    // 	Process();
    // 	//TODO: sleep here is needed.
    //   }
    // } catch (const std::exception &e) {
    //   std::cout <<"Monitor::Exec() Error: Uncaught exception: " <<e.what() <<std::endl;
    // } catch (...) {
    //   std::cout <<"Monitor::Exec() Error: Uncaught unrecognised exception" <<std::endl;
    // }

    //TODO
  }
  
}
