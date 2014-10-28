// vvAPICpp.h

#pragma once

#ifdef PJSIPDLL_EXPORTS
	#define PJSIPDLL_DLL_API __declspec(dllexport)
#else
	#define PJSIPDLL_DLL_API __declspec(dllimport)
#endif


// Structure containing pjsip configuration parameters
// Should be synhronized with appropriate .Net structure!!!!!
struct SipConfigStruct
{
	int listenPort;
	bool noUDP;
	bool noTCP;
	char stunAddress[255];
	bool publishEnabled;
	int expires;

	int maxCalls;

	int soundAutoCloseTime;

	bool VADEnabled;
	int ECTail;

	char nameServer[255];

	bool pollingEventsEnabled;

	int logLevel;
	char logFileName[100];

	// IMS specifics
	bool imsEnabled;
	bool imsIPSecHeaders;
	bool imsIPSecTransport;
};

// callback function definitions
typedef int __stdcall fptr_regstate(int, int);				// on registration state changed
typedef int __stdcall fptr_callstate(int, int);	// on call state changed
typedef int __stdcall fptr_callincoming(int, char*);	// on call incoming
typedef int __stdcall fptr_getconfigdata(int);	// get config data
typedef int __stdcall fptr_callholdconf(int);
typedef int __stdcall fptr_callretrieveconf(int);

typedef int __stdcall fptr_msgrec (char*, char*);
typedef int __stdcall fptr_buddystatus(int, int, const char*);
typedef int __stdcall fptr_dtmfdigit(int callId, int digit);
typedef int __stdcall fptr_mwi(int mwi, char* info);
typedef int __stdcall fptr_crep(int oldid, int newid);

// Callback registration 
extern "C" PJSIPDLL_DLL_API int onRegStateCallback(fptr_regstate cb);	  // register registration notifier
extern "C" PJSIPDLL_DLL_API int onCallStateCallback(fptr_callstate cb); // register call notifier
extern "C" PJSIPDLL_DLL_API int onCallIncoming(fptr_callincoming cb); // register incoming call notifier
extern "C" PJSIPDLL_DLL_API int onCallHoldConfirmCallback(fptr_callholdconf cb); // register call notifier
//extern "C" PJSIPDLL_DLL_API int onCallRetrieveConfirm(fptr_callretrieveconf cb); // register call notifier
extern "C" PJSIPDLL_DLL_API int onMessageReceivedCallback(fptr_msgrec cb); // register call notifier
extern "C" PJSIPDLL_DLL_API int onBuddyStatusChangedCallback(fptr_buddystatus cb); // register call notifier
extern "C" PJSIPDLL_DLL_API int onDtmfDigitCallback(fptr_dtmfdigit cb); // register dtmf digit notifier
extern "C" PJSIPDLL_DLL_API int onMessageWaitingCallback(fptr_mwi cb); // register MWI notifier
extern "C" PJSIPDLL_DLL_API int onCallReplaced(fptr_crep cb); // register Call replaced notifier

// pjsip common API
extern "C" PJSIPDLL_DLL_API void dll_setSipConfig(SipConfigStruct* config);
extern "C" PJSIPDLL_DLL_API int dll_init();
extern "C" PJSIPDLL_DLL_API int dll_shutdown(); 
extern "C" PJSIPDLL_DLL_API int dll_main(void);
extern "C" PJSIPDLL_DLL_API int dll_getNumOfCodecs();
extern "C" PJSIPDLL_DLL_API int dll_getCodec(int index, char* codec);
extern "C" PJSIPDLL_DLL_API int dll_setCodecPriority(char* name, int index);

// pjsip call API
extern "C" PJSIPDLL_DLL_API int dll_registerAccount(char* uri, char* reguri, char* name, char* username, 
																										char* password, char* proxy, bool isdefault);
extern "C" PJSIPDLL_DLL_API int dll_makeCall(int accountId, char* uri); 
extern "C" PJSIPDLL_DLL_API int dll_releaseCall(int callId); 
extern "C" PJSIPDLL_DLL_API int dll_answerCall(int callId, int code);
extern "C" PJSIPDLL_DLL_API int dll_holdCall(int callId);
extern "C" PJSIPDLL_DLL_API int dll_retrieveCall(int callId);
extern "C" PJSIPDLL_DLL_API int dll_xferCall(int callid, char* uri);
extern "C" PJSIPDLL_DLL_API int dll_xferCallWithReplaces(int callId, int dstSession);
extern "C" PJSIPDLL_DLL_API int dll_serviceReq(int callId, int serviceCode, const char* destUri);
extern "C" PJSIPDLL_DLL_API int dll_dialDtmf(int callId, char* digits, int mode);
extern "C" PJSIPDLL_DLL_API int dll_removeAccounts();
extern "C" PJSIPDLL_DLL_API int dll_sendInfo(int callid, char* content);
extern "C" PJSIPDLL_DLL_API int dll_getCurrentCodec(int callId, char* codec);
extern "C" PJSIPDLL_DLL_API int dll_makeConference(int callId);
extern "C" PJSIPDLL_DLL_API int dll_sendCallMessage(int callId, char* message);

// IM & Presence api
extern "C" PJSIPDLL_DLL_API int dll_addBuddy(char* uri, bool subscribe);
extern "C" PJSIPDLL_DLL_API int dll_removeBuddy(int buddyId);
extern "C" PJSIPDLL_DLL_API int dll_sendMessage(int accId, char* uri, char* message);
extern "C" PJSIPDLL_DLL_API int dll_setStatus(int accId, int presence_state);

extern "C" PJSIPDLL_DLL_API int dll_setSoundDevice(char* playbackDeviceId, char* recordingDeviceId);

extern "C" PJSIPDLL_DLL_API int dll_pollForEvents(int timeout);