
#include <stdlib.h>
#include <iostream>
#include <unistd.h>
#include <fstream>

#include <sys/mman.h>
#include <signal.h>
#include <boost/program_options.hpp>
#include "JackClient.hpp"
#include <boost/foreach.hpp>
#include <boost/math/constants/constants.hpp>
#include <cmath>


static volatile bool running = true;

jk::Sample const kTwoPi(2 * boost::math::constants::pi<jk::Sample>());

class ToneGenBase
{
public:
	typedef boost::shared_ptr<ToneGenBase> Ptr;
	typedef jk::OutSampleBuffer OutSampleBuffer;
	typedef jk::Sample Sample;
	virtual ~ToneGenBase() {}
	virtual void operator()(OutSampleBuffer & outBuf) = 0;
protected:
	ToneGenBase()
	{}
};

class ToneGenSet : public jk::JackClient<ToneGenSet>
{
public:
	typedef std::vector<ToneGenBase::Ptr> ToneGens;
	ToneGenSet(std::string serverName, std::string clientName)
		: JackClient(serverName, clientName)
	{}
	void SetGenerators(ToneGens const &tgs)
	{
		toneGens = tgs;
		RegisterPorts(0, tgs.size());
	}
	void operator()(jk::InSampleBuffers const &, jk::OutSampleBuffers & outBufs)
	{
		jk::OutSampleBuffers::iterator osIt(outBufs.begin());
		BOOST_FOREACH(ToneGenBase::Ptr tg, toneGens)
		{
			(*tg)(*osIt);
			++osIt;
		}
	}
	ToneGens toneGens;
};

class GlitsToneGen : public ToneGenBase
{
public:
	GlitsToneGen(unsigned sampleRateHz, char chan, double amplDb)
		: phcount(0)
		, phwrap((sampleRateHz % 1000) ? sampleRateHz/100 : sampleRateHz/1000) // Token nod to 44.1
		, seqcount(0)
		, ampl(std::pow(10, amplDb / 20))
		, right(chan == 'r' || chan == 'R')
		, dPhase(kTwoPi * 1000 / sampleRateHz)
		, intlen(sampleRateHz / 4)
		, seqlen(sampleRateHz * 4)
	{}
	virtual void operator()(OutSampleBuffer & outBuf)
	{
		BOOST_FOREACH(Sample & val, outBuf)
		{
			if (!right && seqcount < intlen) {
				val = 0;
			} else if (right && seqcount >= intlen * 2 && seqcount < intlen * 3) {
				val = 0;
			} else if (right && seqcount >= intlen * 4 && seqcount < intlen * 5) {
				val = 0;
			} else {
				val = ampl * std::sin(phcount * dPhase);
			}
			phcount++;
			if (phcount == phwrap) {
				phcount = 0;
			}
			seqcount++;
			if (seqcount == seqlen) {
				seqcount = 0;
			}
		}
	}
	unsigned phcount;
	unsigned phwrap;
	unsigned seqcount;
	float const ampl;
	bool const right;
	float const dPhase;
	unsigned const intlen;
	unsigned const seqlen;
};

class SimpleTone : public ToneGenBase
{
public:
	SimpleTone(unsigned sampleRateHz, unsigned freqHz, double amplDb)
		: phase(0)
	{
		dPhase = kTwoPi * freqHz / sampleRateHz;
		ampl = std::pow(10, amplDb / 20);
	}
	virtual void operator()(OutSampleBuffer & outBuf)
	{
		BOOST_FOREACH(Sample & val, outBuf)
		{
			val = ampl * std::sin(phase);
			phase += dPhase;
		}
		phase -= std::floor(phase / kTwoPi) * kTwoPi;
	}
	float ampl;
	float phase;
	float dPhase;
};

class ModToneGen : public ToneGenBase
{
public:
	ModToneGen(unsigned sampleRateHz, unsigned freqHz, double amplDb, unsigned modFreqHz, double modPercent)
		: phaseC(0)
		, phaseM(0)
	{
		offset = (200 - modPercent) / modPercent;
		dPhaseC = kTwoPi * freqHz / sampleRateHz;
		dPhaseM = kTwoPi * modFreqHz / sampleRateHz;
		ampl = std::pow(10, amplDb / 20) / (offset + 1);
	}
	void operator()(OutSampleBuffer & outBuf)
	{
		BOOST_FOREACH(Sample & val, outBuf)
		{
			val = ampl * std::sin(phaseC) * (offset + std::sin(phaseM));
			phaseC += dPhaseC;
			phaseM += dPhaseM;
		}
		phaseC -= std::floor(phaseC / kTwoPi) * kTwoPi;
		phaseM -= std::floor(phaseM / kTwoPi) * kTwoPi;
	}
	float ampl;
	float phaseC;
	float phaseM;
	float dPhaseC;
	float dPhaseM;
	float offset;
};



static void sigint_handler (int)
{
    signal (SIGINT, SIG_IGN);
    running = false;
}


int main (int ac, char *av [])
{
    namespace po = boost::program_options;

    po::options_description desc("Options");
    desc.add_options()
        ("help", "produce help message")
        ("test", "test mode - dump output")
        ("client", po::value<std::string>()->default_value("jtone"), "Jack client name (jtone)")
        ("server", po::value<std::string>()->default_value(""), "Jack server name")
        ("out", po::value<std::vector<std::string> >(), "Output Description")
    ;
    po::positional_options_description p;
    p.add("out", -1);

    po::options_description outOpt("Output Description");
    outOpt.add_options()
		("freq", po::value<unsigned>()->default_value(400), "frequency, Hz")
		("ampl", po::value<double>()->default_value(-14.0), "Peak Amplitude, dB")
		("mfreq", po::value<unsigned>()->default_value(0), "modulation frequency, Hz")
		("mod", po::value<double>()->default_value(100.0), "Modulation, percent")
		("glits", po::value<char>()->default_value('-'), "GLITS Channel l/r")
    ;

    po::variables_map vm;
    po::store(po::command_line_parser(ac, av).
              options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        std::cout << outOpt << std::endl;
        return 1;
    }
    std::string clientName("jtone");
    std::string serverName;

    bool testOn = vm.count("test") > 0;
    clientName = vm["client"].as<std::string>();
    serverName = vm["server"].as<std::string>();

    signal (SIGINT, sigint_handler);

    ToneGenSet::SetTestMode(testOn);
    ToneGenSet tgs(serverName, clientName);
    ToneGenSet::ToneGens gens;
	std::cout << "Operating at " << tgs.SampleRateHz() << "Hz" << std::endl;

	std::vector<std::string> const & outvals(vm["out"].as<std::vector<std::string> >());
	BOOST_FOREACH(std::string const & val, outvals)
	{
		std::cout << "Output: " << val << std::endl;
		std::vector<std::string> args = po::split_unix(val);
		po::variables_map outvm;
		po::store(po::command_line_parser(args).options(outOpt).run(), outvm);
		po::notify(outvm);

	    unsigned freq = outvm["freq"].as<unsigned>();
        double ampl = outvm["ampl"].as<double>();
        unsigned modFreq = outvm["mfreq"].as<unsigned>();
        double modPercent = outvm["mod"].as<double>();
        char glitschan = outvm["glits"].as<char>();

		if (glitschan == 'l' || glitschan == 'r')
		{
	        gens.push_back(ToneGenBase::Ptr(new GlitsToneGen(tgs.SampleRateHz(), glitschan, ampl)));
		}
		else if (modFreq)
	    {
	        gens.push_back(ToneGenBase::Ptr(new ModToneGen(tgs.SampleRateHz(), freq, ampl, modFreq, modPercent)));
	    }
	    else
	    {
	        gens.push_back(ToneGenBase::Ptr(new SimpleTone(tgs.SampleRateHz(), freq, ampl)));
	    }
	}
	tgs.SetGenerators(gens);

    while (running)
    {
    	usleep(50000);
    	if (testOn)
    	{
			jk::OutSampleBuffers testBuf;
			float foo[1024];
			testBuf.push_back(jk::OutSampleBuffer(&foo, 1024));
			tgs(jk::InSampleBuffers(), testBuf);
			BOOST_FOREACH(float val, foo)
			{
				std::cout << val << std::endl;
			}
    	}
    }
	std::cout << std::endl << "Bailing" << std::endl;

    return 0;
}



