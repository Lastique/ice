// **********************************************************************
//
// Copyright (c) 2001
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#include <Ice/Ice.h>
#include <TestCommon.h>
#include <Test.h>

using namespace std;

int
run(int argc, char* argv[], const Ice::CommunicatorPtr& communicator)
{
    Test::MyClassPrx allTests(const Ice::CommunicatorPtr&);
    Test::MyClassPrx myClass = allTests(communicator);

    cout << "testing server shutdown... " << flush;
    myClass->shutdown();
    try
    {
	myClass->opVoid();
	test(false);
    }
    catch(const Ice::ConnectFailedException&)
    {
	cout << "ok" << endl;
    }
    catch(const Ice::ConnectionLostException&) // For Java compatibility
    {
	cout << "ok" << endl;
    }

    return EXIT_SUCCESS;
}

int
main(int argc, char* argv[])
{
    int status;
    Ice::CommunicatorPtr communicator;

    try
    {
	communicator = Ice::initialize(argc, argv);
	status = run(argc, argv, communicator);
    }
    catch(const Ice::Exception& ex)
    {
	cerr << ex << endl;
	status = EXIT_FAILURE;
    }

    if (communicator)
    {
	try
	{
	    communicator->destroy();
	}
	catch(const Ice::Exception& ex)
	{
	    cerr << ex << endl;
	    status = EXIT_FAILURE;
	}
    }

    return status;
}
