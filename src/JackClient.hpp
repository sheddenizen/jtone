#ifndef JACKCLIENT_HPP
#define JACKCLIENT_HPP

#include <jack/types.h>
#include <jack/jack.h>
#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/static_assert.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/array.hpp>
#include <boost/noncopyable.hpp>

namespace jk
{

template<typename T>
class BufferWrapT
{
public:
	typedef T value_type;
	typedef value_type * iterator;
	typedef value_type const const_iterator;
	typedef value_type & reference;
	typedef value_type const & const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;


	BufferWrapT(void *begin = 0, size_type size = 0)
		: myBegin(reinterpret_cast<value_type *>(begin))
		, myEnd(reinterpret_cast<value_type *>(begin) + size)
	{}
    iterator        begin()       { return myBegin; }
    const_iterator  begin() const { return myBegin; }
    const_iterator cbegin() const { return myBegin; }

    iterator        end()       { return myEnd; }
    const_iterator  end() const { return myEnd; }
    const_iterator cend() const { return myEnd; }

    reverse_iterator rbegin() { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const {
        return const_reverse_iterator(end());
    }
    const_reverse_iterator crbegin() const {
        return const_reverse_iterator(end());
    }

    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const {
        return const_reverse_iterator(begin());
    }
    const_reverse_iterator crend() const {
        return const_reverse_iterator(begin());
    }

    reference operator[](size_type i)
    {
    	assert((myBegin + i) < myEnd);
    	return myBegin[i];
    }
    const_reference operator[](size_type i) const
    {
    	assert((myBegin + i) < myEnd);
    	return myBegin[i];
    }
    reference front() { assert(size()); return *myBegin; }
    const_reference front() const { assert(size()); return *myBegin; }
    reference back() { assert(size()); return *(myEnd -1 ); }
    const_reference back() const { assert(size()); return *(myEnd - 1); }

    size_type size() const { return myEnd - myBegin; }
private:
	iterator myBegin;
	iterator myEnd;
};

typedef float Sample;
typedef BufferWrapT<Sample const> InSampleBuffer;
typedef BufferWrapT<Sample> OutSampleBuffer;
typedef std::vector<InSampleBuffer> InSampleBuffers;
typedef std::vector<OutSampleBuffer> OutSampleBuffers;

template <typename Derived>
class JackClient : private boost::noncopyable
{
public:
	struct Error : public std::exception
	{
		virtual ~Error() throw() {}
		Error(std::string what)
				: whatStr(what)
		{}
		virtual char const * what()
		{
			return whatStr.c_str();
		}
		std::string whatStr;
	};

	virtual ~JackClient() {}
	double SampleRateHz() const { return myFreq; }
	double BufferSize() const { return myBufSize; }
	bool IsActive() const { return myActive; }
	static void SetTestMode(bool testOn)
	{
		TestMode() = testOn;
	}
	static bool GetTestMode()
	{
		return TestMode();
	}

protected:
	typedef std::vector<jack_port_t* > JackPorts;
	typedef JackClient<Derived> BaseType;

	JackClient(std::string serverName, std::string clientName)
	 : myClientName(clientName)
	 , myActive(false)
	{
		if (GetTestMode())
		{
			myFreq = 88200;
			myBufSize = 1024;
			return;
		}

	    {
		    jack_status_t  stat;
			int opts(JackNoStartServer);
			if (serverName.size())
			{
				opts |= JackServerName;
			}
			jack_client_t * client = jack_client_open (clientName.c_str(), jack_options_t(opts), &stat, serverName.c_str());

			if (!client)
			{
				throw Error("Can't connect to JACK.\n");
			}
			myClient.reset(client, DeleteClient());
	    }

	    jack_on_shutdown (myClient.get(), &shutdown_callback, (void *) this);
	    jack_set_process_callback (myClient.get(), &proc_callback, (void *) this);
	    if (jack_activate (myClient.get()))
	    {
	        throw Error("Can't activate JACK.\n");
	    }

	    myClientName = jack_get_client_name (myClient.get());
	    myFreq = jack_get_sample_rate (myClient.get());
	    myBufSize = jack_get_buffer_size (myClient.get());
	}
	virtual void handle_shutdown() {}

	void RegisterPorts(unsigned chanIn, unsigned chanOut)
	{
		assert(!myActive);

		myJackInputs.resize(chanIn, 0);
		myJackOutputs.resize(chanOut, 0);
		myInputSampleBuffers.resize(chanIn);
		myOutputSampleBuffers.resize(chanOut);
	    std::generate(myJackInputs.begin(), myJackInputs.end(), RegisterPort(myClient, false));
	    std::generate(myJackOutputs.begin(), myJackOutputs.end(), RegisterPort(myClient, true));
	    myActive = true;
	}
private:
	unsigned myFreq;
	unsigned myBufSize;
	std::string myClientName;
	bool myActive;

	boost::shared_ptr<jack_client_t> myClient;
	JackPorts myJackInputs;
	JackPorts myJackOutputs;
	InSampleBuffers myInputSampleBuffers;
	OutSampleBuffers myOutputSampleBuffers;

	static int proc_callback(jack_nframes_t nFrames, void *arg)
	{
		BaseType * bThis (reinterpret_cast<BaseType *>(arg));
		if (!arg || !bThis->myActive)
		{
			assert(arg);
			return 0;
		}
		Derived * dThis (static_cast<Derived *>(bThis));
		BaseType const * kBThis (static_cast<BaseType const *>(bThis));

		std::transform(kBThis->myJackInputs.begin(), kBThis->myJackInputs.end(), bThis->myInputSampleBuffers.begin(), GetBufferT<InSampleBuffer::value_type>(nFrames));
		std::transform(kBThis->myJackOutputs.begin(), kBThis->myJackOutputs.end(), bThis->myOutputSampleBuffers.begin(), GetBufferT<OutSampleBuffer::value_type>(nFrames));

		(*dThis)(kBThis->myInputSampleBuffers, bThis->myOutputSampleBuffers);
		return 0;
	}
    static void shutdown_callback(void *arg)
    {
		assert(arg);
		Derived * derivedThis (reinterpret_cast<Derived *>(arg));
		derivedThis->handle_shutdown();
    }
	static bool & TestMode()
	{
		static bool testOn = false;
		return testOn;
	}
    template <typename T>
	struct GetBufferT
	{
		GetBufferT(jack_nframes_t n)
			: nFrames(n)
		{}
		BufferWrapT<T> operator()(JackPorts::value_type port)
		{
			return BufferWrapT<T>(jack_port_get_buffer (port, nFrames), nFrames);
		}
		boost::shared_ptr<jack_client_t> client;
		jack_nframes_t nFrames;
	};
	struct DeleteClient
	{
		void operator()(jack_client_t *clientPtr)
		{
			if (clientPtr)
			{
			    jack_deactivate (clientPtr);
			    jack_client_close (clientPtr);
			}
		}
	};
	struct RegisterPort
	{
		RegisterPort(boost::shared_ptr<jack_client_t> c, bool o)
			: client(c)
			, isOut(o)
			, idx(1)
		{
			if (isOut)
			{
				portNameBase = "out_";
			}
			else
			{
				portNameBase = "in_";
			}
		}

		jack_port_t* operator()()
		{
			std::string portName(portNameBase);
			if (idx < 10)
			{
				portName += '0';
			}
			portName += boost::lexical_cast<std::string>(idx);
			++idx;
			return jack_port_register (client.get(), portName.c_str(), JACK_DEFAULT_AUDIO_TYPE,
					isOut ? JackPortIsOutput : JackPortIsInput, 0);
		}
		boost::shared_ptr<jack_client_t> client;
		std::string portNameBase;
		bool isOut;
		int idx;
	};
 };


} //namespace jk


#endif // JACKCLIENT_HPP
