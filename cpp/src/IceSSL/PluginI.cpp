// **********************************************************************
//
// Copyright (c) 2003-2006 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <PluginI.h>
#include <Instance.h>
#include <Util.h>
#include <Ice/BuiltinSequences.h>
#include <Ice/Communicator.h>
#include <Ice/LocalException.h>
#include <Ice/Logger.h>
#include <Ice/Properties.h>
#include <IceUtil/StaticMutex.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

using namespace std;
using namespace Ice;
using namespace IceSSL;

#ifndef ICE_SSL_API
#   ifdef ICE_SSL_API_EXPORTS
#       define ICE_SSL_API ICE_DECLSPEC_EXPORT
#    else
#       define ICE_SSL_API ICE_DECLSPEC_IMPORT
#    endif
#endif

//
// Plugin factory function.
//
extern "C"
{

ICE_SSL_API Ice::Plugin*
create(const CommunicatorPtr& communicator, const string& name, const StringSeq& args)
{
    PluginI* plugin = new PluginI(communicator);
    return plugin;
}

}

static IceUtil::StaticMutex staticMutex = ICE_STATIC_MUTEX_INITIALIZER;
static int instanceCount = 0;
static IceUtil::Mutex* locks = 0;

//
// OpenSSL mutex callback.
//
static void opensslLockCallback(int mode, int n, const char* file, int line)
{
    if(mode & CRYPTO_LOCK)
    {
	locks[n].lock();
    }
    else
    {
	locks[n].unlock();
    }
}

//
// OpenSSL thread id callback.
//
static unsigned long
opensslThreadIdCallback()
{
#if defined(_WIN32)
    return static_cast<unsigned long>(GetCurrentThreadId());
#elif defined(__FreeBSD__) || defined(__APPLE__) || defined(__osf1__)
    //
    // On some platforms, pthread_t is a pointer to a per-thread structure.
    // 
    return reinterpret_cast<unsigned long>(pthread_self());
#elif (defined(__linux) || defined(__sun) || defined(__hpux)) || defined(_AIX)
    //
    // On Linux, Solaris, HP-UX and AIX, pthread_t is an integer.
    //
    return static_cast<unsigned long>(pthread_self());
#else
#   error "Unknown platform"
#endif
}

//
// VerifyInfo constructor.
//
IceSSL::VerifyInfo::VerifyInfo() :
    incoming(false),
    cert(0),
    ssl(0)
{
}

//
// Plugin implementation.
//
IceSSL::PluginI::PluginI(const Ice::CommunicatorPtr& communicator)
{
    setupSSL(communicator);

    _instance = new Instance(communicator);
}

void
IceSSL::PluginI::destroy()
{
    _instance->destroy();
    _instance = 0;

    cleanupSSL();
}

void
IceSSL::PluginI::initialize(SSL_CTX* clientContext, SSL_CTX* serverContext)
{
    _instance->initialize(clientContext, serverContext);
}

void
IceSSL::PluginI::setCertificateVerifier(const CertificateVerifierPtr& verifier)
{
    _instance->setCertificateVerifier(verifier);
}

void
IceSSL::PluginI::setPasswordPrompt(const PasswordPromptPtr& prompt)
{
    _instance->setPasswordPrompt(prompt);
}

SSL_CTX*
IceSSL::PluginI::clientContext()
{
    return _instance->clientContext()->ctx();
}

SSL_CTX*
IceSSL::PluginI::serverContext()
{
    return _instance->serverContext()->ctx();
}

void
IceSSL::PluginI::setupSSL(const CommunicatorPtr& communicator)
{
    //
    // Initialize OpenSSL.
    //
    IceUtil::StaticMutex::Lock sync(staticMutex);
    instanceCount++;

    if(instanceCount == 1)
    {
	PropertiesPtr properties = communicator->getProperties();

	//
	// Create the mutexes and set the callbacks.
	//
	locks = new IceUtil::Mutex[CRYPTO_num_locks()];
	CRYPTO_set_locking_callback(opensslLockCallback);
	CRYPTO_set_id_callback(opensslThreadIdCallback);

	//
	// Load human-readable error messages.
	//
	SSL_load_error_strings();

	//
	// Initialize the SSL library.
	//
	SSL_library_init();

	//
	// Initialize the PRNG.
	//
#ifdef WINDOWS
	RAND_screen(); // Uses data from the screen if possible.
#endif
	char randFile[1024];
	if(RAND_file_name(randFile, sizeof(randFile))) // Gets the name of a default seed file.
	{
	    RAND_load_file(randFile, 1024);
	}
	string randFiles = properties->getProperty("IceSSL.Random");
	if(!randFiles.empty())
	{
	    vector<string> files;
#ifdef _WIN32
	    const string sep = ";";
#else
	    const string sep = ":";
#endif
	    if(!splitString(randFiles, sep, false, files))
	    {
		PluginInitializationException ex(__FILE__, __LINE__);
		ex.reason = "IceSSL: invalid value for IceSSL.Random:\n" + randFiles;
		throw ex;
	    }
	    for(vector<string>::iterator p = files.begin(); p != files.end(); ++p)
	    {
		if(!RAND_load_file(p->c_str(), 1024))
		{
		    PluginInitializationException ex(__FILE__, __LINE__);
		    ex.reason = "IceSSL: unable to load entropy data from " + *p;
		    throw ex;
		}
	    }
	}
#ifndef _WIN32
	//
	// The Entropy Gathering Daemon (EGD) is not available on Windows.
	// The file should be a Unix domain socket for the daemon.
	//
	string entropyDaemon = properties->getProperty("IceSSL.EntropyDaemon");
	if(!entropyDaemon.empty())
	{
	    if(RAND_egd(entropyDaemon.c_str()) <= 0)
	    {
		PluginInitializationException ex(__FILE__, __LINE__);
		ex.reason = "IceSSL: EGD failure using file " + entropyDaemon;
		throw ex;
	    }
	}
#endif
	if(!RAND_status())
	{
	    communicator->getLogger()->warning("IceSSL: insufficient data to initialize PRNG");
	}
    }
}

void
IceSSL::PluginI::cleanupSSL()
{
    IceUtil::StaticMutex::Lock sync(staticMutex);

    if(--instanceCount == 0)
    {
	CRYPTO_set_locking_callback(0);
	CRYPTO_set_id_callback(0);
	delete[] locks;
	locks = 0;

	CRYPTO_cleanup_all_ex_data();
	RAND_cleanup();
	ERR_free_strings();
	EVP_cleanup();
    }
}
