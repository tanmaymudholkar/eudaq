#include "Configuration.hh"
#include "Producer.hh"
#include "eudaq/Utils.hh"
#include "eudaq/Logger.hh"
#include "eudaq/RawDataEvent.hh"

#include "TLUController.hh"

#include <iostream>
#include <ostream>
#include <cctype>
#include <memory>

using eudaq::to_string;
using eudaq::to_hex;
using namespace tlu;
// #ifdef _WIN32
// ZESTSC1_ERROR_FUNC ZestSC1_ErrorHandler = NULL;
// // Windows needs some parameters for this. i dont know where it will
// // be called so we need to check it in future
// char *ZestSC1_ErrorStrings[] = {"bla bla", "blub"};
// #endif

class TLUProducer : public eudaq::Producer {
public:
  TLUProducer(const std::string name, const std::string &runcontrol); //TODO: check para no ref
  void DoConfigure() override;
  void DoStartRun() override;
  void DoStopRun() override;
  void DoTerminate() override;
  void DoReset() override;
  void Exec() override;

  void OnStatus() override;
  void OnUnrecognised(const std::string &cmd, const std::string &param) override;

  void MainLoop();

  static const uint32_t m_id_factory = eudaq::cstr2hash("TluProducer");
private:
  
  unsigned m_run, m_ev;
  unsigned trigger_interval, dut_mask, veto_mask, and_mask, or_mask,
    pmtvcntl[TLU_PMTS], pmtvcntlmod;
  uint32_t strobe_period, strobe_width;
  unsigned enable_dut_veto, handshake_mode;
  unsigned trig_rollover, readout_delay;
  bool timestamps, done, timestamp_per_run;
  bool TLUStarted;
  bool TLUJustStopped;
  uint64_t lasttime;
  std::shared_ptr<TLUController> m_tlu;
  std::string pmt_id[TLU_PMTS];
  double pmt_gain_error[TLU_PMTS], pmt_offset_error[TLU_PMTS];
  uint32_t m_id_stream;
};

namespace{
  auto dummy0 = eudaq::Factory<eudaq::Producer>::
    Register<TLUProducer, const std::string&, const std::string&>(TLUProducer::m_id_factory);
}

TLUProducer::TLUProducer(const std::string name, const std::string &runcontrol)
  : eudaq::Producer(name, runcontrol), m_run(0), m_ev(0),
  trigger_interval(0), dut_mask(0), veto_mask(0), and_mask(255),
  or_mask(0), pmtvcntlmod(0), strobe_period(0), strobe_width(0),
  enable_dut_veto(0), trig_rollover(0), readout_delay(100),
  timestamps(true), done(false), timestamp_per_run(false),
  TLUStarted(false), TLUJustStopped(false), lasttime(0), m_tlu(0) {
  for (int i = 0; i < TLU_PMTS; i++) {
    pmtvcntl[i] = PMT_VCNTL_DEFAULT;
    pmt_id[i] = "<unknown>";
    pmt_gain_error[i] = 1.0;
    pmt_offset_error[i] = 0.0;
  }
  m_id_stream = eudaq::cstr2hash(name.c_str());

}

void TLUProducer::Exec(){
  StartCommandReceiver();
  while(IsActiveCommandReceiver()){
    MainLoop();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}
  
void TLUProducer::MainLoop() {
  do {
    if (!m_tlu) {
      eudaq::mSleep(50);
      continue;
    }
    bool JustStopped = TLUJustStopped;
    if (JustStopped) {
      m_tlu->Stop();
      eudaq::mSleep(100);
    }
    if (TLUStarted || JustStopped) {
      eudaq::mSleep(readout_delay);
      m_tlu->Update(timestamps); // get new events
      if (trig_rollover > 0 && m_tlu->GetTriggerNum() > trig_rollover) {
	bool inhibit = m_tlu->InhibitTriggers();
	m_tlu->ResetTriggerCounter();
	m_tlu->InhibitTriggers(inhibit);
      }
      // std::cout << "--------" << std::endl;
      for (size_t i = 0; i < m_tlu->NumEntries(); ++i) {
	m_ev = m_tlu->GetEntry(i).Eventnum();
	uint64_t t = m_tlu->GetEntry(i).Timestamp();
	int64_t d = t - lasttime;
	// float freq= 1./(d*20./1000000000);
	float freq = 1. / Timestamp2Seconds(d);
	if (m_ev < 10 || m_ev % 1000 == 0) {
	  std::cout << "  " << m_tlu->GetEntry(i) << ", diff=" << d
		    << (d <= 0 ? "  ***" : "") << ", freq=" << freq
		    << std::endl;
	}
	lasttime = t;
	auto ev = eudaq::RawDataEvent::MakeUnique("TluRawDataEvent");
	ev->SetTimestamp(t, t+1);//TODO, duration
	  
	ev->SetTag("trigger", m_tlu->GetEntry(i).trigger2String());
	if (i == m_tlu->NumEntries() - 1) {
	  ev->SetTag("PARTICLES", to_string(m_tlu->GetParticles()));
	  for (int i = 0; i < TLU_TRIGGER_INPUTS; ++i) {
	    ev->SetTag("SCALER" + to_string(i),
		       to_string(m_tlu->GetScaler(i)));
	  }
	}
	SendEvent(std::move(ev));
      }
    }
    if (JustStopped) {
      m_tlu->Update(timestamps);
      auto ev = eudaq::RawDataEvent::MakeUnique("TluRawDataEvent");
      ev->SetEORE();
      SendEvent(std::move(ev));
      TLUJustStopped = false;
    }
  } while (!done);
}

void TLUProducer::DoConfigure() {
  auto conf = GetConfiguration();
  std::cout << "Configuring (" << conf->Name() << ")..." << std::endl;
  if (m_tlu)
    m_tlu = 0;
  int errorhandler = conf->Get("ErrorHandler", 2);
  m_tlu = std::make_shared<TLUController>(errorhandler);

  trigger_interval = conf->Get("TriggerInterval", 0);
  dut_mask = conf->Get("DutMask", 2);
  and_mask = conf->Get("AndMask", 0xff);
  or_mask = conf->Get("OrMask", 0);
  strobe_period = conf->Get("StrobePeriod", 0);
  strobe_width = conf->Get("StrobeWidth", 0);
  enable_dut_veto = conf->Get("EnableDUTVeto", 0);
  handshake_mode = conf->Get("HandShakeMode", 63);
  veto_mask = conf->Get("VetoMask", 0);
  trig_rollover = conf->Get("TrigRollover", 0);
  timestamps = conf->Get("Timestamps", 1);
  for (int i = 0; i < TLU_PMTS;
       i++) // Override with any individually set values
    {
      pmtvcntl[i] = (unsigned)conf->Get("PMTVcntl" + to_string(i + 1),
					"PMTVcntl", PMT_VCNTL_DEFAULT);
      pmt_id[i] = conf->Get("PMTID" + to_string(i + 1), "<unknown>");
      pmt_gain_error[i] = conf->Get("PMTGainError" + to_string(i + 1), 1.0);
      pmt_offset_error[i] =
	conf->Get("PMTOffsetError" + to_string(i + 1), 0.0);
    }
  pmtvcntlmod = conf->Get("PMTVcntlMod", 0); // If 0, it's a standard TLU;
  // if 1, the DAC output voltage
  // is doubled
  readout_delay = conf->Get("ReadoutDelay", 1000);
  timestamp_per_run = conf->Get("TimestampPerRun", 0);
  // ***
  m_tlu->SetDebugLevel(conf->Get("DebugLevel", 0));
  m_tlu->SetFirmware(conf->Get("BitFile", ""));
  m_tlu->SetVersion(conf->Get("Version", 0));
  m_tlu->Configure();
  for (int i = 0; i < tlu::TLU_LEMO_DUTS; ++i) {
    m_tlu->SelectDUT(conf->Get("DUTInput", "DUTInput" + to_string(i), "RJ45"), 1 << i,
		     false);
  }
  m_tlu->SetTriggerInterval(trigger_interval);
  m_tlu->SetPMTVcntlMod(pmtvcntlmod);
  m_tlu->SetPMTVcntl(pmtvcntl, pmt_gain_error, pmt_offset_error);
  m_tlu->SetDUTMask(dut_mask);
  m_tlu->SetVetoMask(veto_mask);
  m_tlu->SetAndMask(and_mask);
  m_tlu->SetOrMask(or_mask);
  m_tlu->SetStrobe(strobe_period, strobe_width);
  m_tlu->SetEnableDUTVeto(enable_dut_veto);
  m_tlu->SetHandShakeMode(handshake_mode);
  m_tlu->SetTriggerInformation(USE_TRIGGER_INPUT_INFORMATION);
  m_tlu->ResetTimestamp();

  // by dhaas
  eudaq::mSleep(1000);

  m_tlu->Update(timestamps);
  std::cout << "...Configured (" << conf->Name() << ")" << std::endl;
  EUDAQ_INFO("Configured (" + conf->Name() + ")");
}

void TLUProducer::DoStartRun(){
  m_run = GetRunNumber();
  m_ev = 0;
  std::cout << "Start Run " << std::endl;
  auto ev = eudaq::RawDataEvent::MakeUnique("TluRawDataEvent");
  ev->SetBORE();
  ev->SetTag("FirmwareID", to_string(m_tlu->GetFirmwareID()));
  ev->SetTag("TriggerInterval", to_string(trigger_interval));
  ev->SetTag("DutMask", "0x" + to_hex(dut_mask));
  ev->SetTag("AndMask", "0x" + to_hex(and_mask));
  ev->SetTag("OrMask", "0x" + to_hex(or_mask));
  ev->SetTag("VetoMask", "0x" + to_hex(veto_mask));
  for (int i = 0; i < TLU_PMTS;
       i++) // Separate loops so they are sequential in file
    {
      ev->SetTag("PMTID" + to_string(i + 1), pmt_id[i]);
    }
  ev->SetTag("PMTVcntlMod", to_string(pmtvcntlmod));
  for (int i = 0; i < TLU_PMTS; i++) {
    ev->SetTag("PMTVcntl" + to_string(i + 1), to_string(pmtvcntl[i]));
  }
  for (int i = 0; i < TLU_PMTS;
       i++) // Separate loops so they are sequential in file
    {
      ev->SetTag("PMTGainError" + to_string(i + 1), pmt_gain_error[i]);
    }
  for (int i = 0; i < TLU_PMTS;
       i++) // Separate loops so they are sequential in file
    {
      ev->SetTag("PMTOffsetError" + to_string(i + 1), pmt_offset_error[i]);
    }
  ev->SetTag("ReadoutDelay", to_string(readout_delay));
  ev->SetTag("TimestampZero", to_string(m_tlu->TimestampZero()));
  eudaq::mSleep(5000); // temporarily, to fix startup with EUDRB
  SendEvent(std::move(ev));
  if (timestamp_per_run)
    m_tlu->ResetTimestamp();
  eudaq::mSleep(5000);
  m_tlu->ResetTriggerCounter();
  if (timestamp_per_run)
    m_tlu->ResetTimestamp();
  m_tlu->ResetScalers();
  m_tlu->Update(timestamps);
  m_tlu->Start();
  TLUStarted = true;
}

void TLUProducer::DoStopRun(){
  std::cout << "Stop Run" << std::endl;
  TLUStarted = false;
  TLUJustStopped = true;
  while (TLUJustStopped) {
    eudaq::mSleep(100);
  }
}

void TLUProducer::DoTerminate(){
  std::cout << "Terminate (press enter)" << std::endl;
  done = true;
  eudaq::mSleep(1000);
}

void TLUProducer::DoReset(){
  std::cout << "Reset" << std::endl;
  m_tlu->Stop();        // stop
  m_tlu->Update(false); // empty events
}

void TLUProducer::OnStatus(){
  SetStatusTag("TRIG", to_string(m_ev));
  if (m_tlu) {
    SetStatusTag("TIMESTAMP",
		 to_string(Timestamp2Seconds(m_tlu->GetTimestamp())));
    SetStatusTag("LASTTIME", to_string(Timestamp2Seconds(lasttime)));
    SetStatusTag("PARTICLES", to_string(m_tlu->GetParticles()));
    SetStatusTag("STATUS", m_tlu->GetStatusString());
    for (int i = 0; i < 4; ++i) {
      SetStatusTag("SCALER" + to_string(i),
		   to_string(m_tlu->GetScaler(i)));
    }
  }
}

void TLUProducer::OnUnrecognised(const std::string &cmd,
				 const std::string &param){
  std::cout << "Unrecognised: (" << cmd.length() << ") " << cmd;
  if (param.length() > 0)
    std::cout << " (" << param << ")";
  std::cout << std::endl;
  EUDAQ_WARN("Unrecognised command");
}
