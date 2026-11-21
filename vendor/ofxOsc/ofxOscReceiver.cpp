// copyright (c) openFrameworks team 2010-2017
// copyright (c) Damian Stewart 2007-2009
#include "ofxOscReceiver.h"
#include "TimerListener.h"
#include <atomic>

using namespace std;

// This hook exists only in the standalone lifecycle test build.
#ifdef VOSC_RECEIVER_LIFECYCLE_TESTING
void voscReceiverLifecycleHook(const char* phase);
#define RECEIVER_TEST_HOOK(phase) voscReceiverLifecycleHook(phase)
#else
#define RECEIVER_TEST_HOOK(phase) ((void)0)
#endif

// oscpack's Break flag stays confined to the receive thread. The periodic
// timer wakes select even without OSC traffic and observes a separate atomic
// request, including a request made before Run resets its internal Break flag.
struct ofxOscReceiver::ReceiveState : osc::TimerListener {
    osc::UdpReceiveSocket socket;
    osc::SocketReceiveMultiplexer multiplexer;
    osc::PacketListener* listener;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> running{true};

    ReceiveState(const ofxOscReceiverSettings& settings, osc::PacketListener* listener)
    : socket(osc::IpEndpointName(osc::IpEndpointName::ANY_ADDRESS, settings.port), settings.reuse)
    , listener(listener) {
        multiplexer.AttachSocketListener(&socket, listener);
        multiplexer.AttachPeriodicTimerListener(10, this);
    }

    ~ReceiveState() override {
        multiplexer.DetachPeriodicTimerListener(this);
        multiplexer.DetachSocketListener(&socket, listener);
    }

    void TimerExpired() override {
        if (stopRequested.load()) multiplexer.Break();
    }

    void run() noexcept {
        try {
            RECEIVER_TEST_HOOK("before_run");
            multiplexer.Run();
        } catch (const std::exception& e) {
            ofLogWarning("ofxOscReceiver") << "receiver stopped: " << e.what();
        } catch (...) {
            ofLogWarning("ofxOscReceiver") << "receiver stopped after unknown exception";
        }
        running.store(false);
        RECEIVER_TEST_HOOK("worker_finished");
    }
};

ofxOscReceiver::ofxOscReceiver() = default;

//--------------------------------------------------------------
ofxOscReceiver::~ofxOscReceiver(){
	stop();
}

//--------------------------------------------------------------
ofxOscReceiver::ofxOscReceiver(const ofxOscReceiver &mom){
	copy(mom);
}

//--------------------------------------------------------------
ofxOscReceiver& ofxOscReceiver::operator=(const ofxOscReceiver &mom){
	return copy(mom);
}

//--------------------------------------------------------------
ofxOscReceiver& ofxOscReceiver::copy(const ofxOscReceiver &other){
    if(this == &other) return *this;
    const bool listening = other.isListening();
    stop();
    settings = other.settings;
    if(listening) start();
    return *this;
}

//--------------------------------------------------------------
bool ofxOscReceiver::setup(int port){
    stop();
    settings.port = port;
    return start();
}

//--------------------------------------------------------------
bool ofxOscReceiver::setup(const ofxOscReceiverSettings &settings) {
    stop();
    this->settings = settings;
    return settings.start ? start() : true;
}

//--------------------------------------------------------------
bool ofxOscReceiver::start() {
    if(isListening()) return true;
    // An exited worker is still joinable and owns state until the owner reaps it.
    stop();
    if(osc::UdpSocket::GetUdpBufferSize() == 0){
        osc::UdpSocket::SetUdpBufferSize(65535);
    }
    try {
        receiveState = std::make_unique<ReceiveState>(settings, this);
        auto* state = receiveState.get();
        RECEIVER_TEST_HOOK("before_launch");
        listenThread = std::thread([state]{ state->run(); });
    } catch(const std::exception& e) {
        receiveState.reset();
        ofLogError("ofxOscReceiver") << "couldn't create receiver on port "
                                    << settings.port << ": " << e.what();
        return false;
    }
    return true;
}

//--------------------------------------------------------------
void ofxOscReceiver::stop() {
    if(receiveState) {
        receiveState->stopRequested.store(true);
        RECEIVER_TEST_HOOK("stop_requested");
    }
    if(listenThread.joinable()) listenThread.join();
    receiveState.reset();
}

//--------------------------------------------------------------
bool ofxOscReceiver::isListening() const{
    return receiveState && receiveState->running.load();
}

//--------------------------------------------------------------
bool ofxOscReceiver::hasWaitingMessages() const{
	return !messagesChannel.empty();
}

//--------------------------------------------------------------
bool ofxOscReceiver::getNextMessage(ofxOscMessage *message){
	return getNextMessage(*message);
}

//--------------------------------------------------------------
bool ofxOscReceiver::getNextMessage(ofxOscMessage &message){
	return messagesChannel.tryReceive(message);
}

//--------------------------------------------------------------
bool ofxOscReceiver::getParameter(ofAbstractParameter &parameter){
	ofxOscMessage msg;
	while(messagesChannel.tryReceive(msg)){
		ofAbstractParameter * p = &parameter;
		std::vector<std::string> address = ofSplitString(msg.getAddress(),"/", true);
		for(unsigned int i = 0; i < address.size(); i++){
			if(p){
				if(address[i] == p->getEscapedName()){
					if(p->type() == typeid(ofParameterGroup).name()){
						if(address.size() >= i+1){
							ofParameterGroup* g = static_cast<ofParameterGroup*>(p);
							if(g->contains(address[i+1])){
								p = &g->get(address[i+1]);
							}
							else{
								p = nullptr;
							}
						}
					}
					else if(p->type() == typeid(ofParameter<int>).name() &&
						msg.getArgType(0) == OFXOSC_TYPE_INT32){
						p->cast<int>() = msg.getArgAsInt32(0);
					}
					else if(p->type() == typeid(ofParameter<float>).name() &&
						msg.getArgType(0) == OFXOSC_TYPE_FLOAT){
						p->cast<float>() = msg.getArgAsFloat(0);
					}
					else if(p->type() == typeid(ofParameter<double>).name() &&
						msg.getArgType(0) == OFXOSC_TYPE_DOUBLE){
						p->cast<double>() = msg.getArgAsDouble(0);
					}
					else if(p->type() == typeid(ofParameter<bool>).name() &&
						(msg.getArgType(0) == OFXOSC_TYPE_TRUE ||
						 msg.getArgType(0) == OFXOSC_TYPE_FALSE ||
						 msg.getArgType(0) == OFXOSC_TYPE_INT32 ||
						 msg.getArgType(0) == OFXOSC_TYPE_INT64 ||
						 msg.getArgType(0) == OFXOSC_TYPE_FLOAT ||
						 msg.getArgType(0) == OFXOSC_TYPE_DOUBLE ||
						 msg.getArgType(0) == OFXOSC_TYPE_STRING ||
						 msg.getArgType(0) == OFXOSC_TYPE_SYMBOL)){
						p->cast<bool>() = msg.getArgAsBool(0);
					}
					else if(msg.getArgType(0) == OFXOSC_TYPE_STRING){
						p->fromString(msg.getArgAsString(0));
					}
				}
			}
		}
	}
	return true;
}

//--------------------------------------------------------------
int ofxOscReceiver::getPort() const{
	return settings.port;
}

//--------------------------------------------------------------
const ofxOscReceiverSettings &ofxOscReceiver::getSettings() const {
	return settings;
}

// PROTECTED
//--------------------------------------------------------------
void ofxOscReceiver::ProcessMessage(const osc::ReceivedMessage &m, const osc::IpEndpointName &remoteEndpoint){
	// convert the message to an ofxOscMessage
	ofxOscMessage msg;

	// set the address
	msg.setAddress(m.AddressPattern());

	// set the sender ip/host
	char endpointHost[osc::IpEndpointName::ADDRESS_STRING_LENGTH];
	remoteEndpoint.AddressAsString(endpointHost);
	msg.setRemoteEndpoint(endpointHost, remoteEndpoint.port);

	// transfer the arguments
	for(osc::ReceivedMessage::const_iterator arg = m.ArgumentsBegin(); arg != m.ArgumentsEnd(); ++arg){
		if(arg->IsInt32()){
			msg.addIntArg(arg->AsInt32Unchecked());
		}
		else if(arg->IsInt64()){
			msg.addInt64Arg(arg->AsInt64Unchecked());
		}
		else if( arg->IsFloat()){
			msg.addFloatArg(arg->AsFloatUnchecked());
		}
		else if(arg->IsDouble()){
			msg.addDoubleArg(arg->AsDoubleUnchecked());
		}
		else if(arg->IsString()){
			msg.addStringArg(arg->AsStringUnchecked());
		}
		else if(arg->IsSymbol()){
			msg.addSymbolArg(arg->AsSymbolUnchecked());
		}
		else if(arg->IsChar()){
			msg.addCharArg(arg->AsCharUnchecked());
		}
		else if(arg->IsMidiMessage()){
			msg.addMidiMessageArg(arg->AsMidiMessageUnchecked());
		}
		else if(arg->IsBool()){
			msg.addBoolArg(arg->AsBoolUnchecked());
		}
		else if(arg->IsNil()){
			msg.addNoneArg();
		}
		else if(arg->IsInfinitum()){
			msg.addTriggerArg();
		}
		else if(arg->IsTimeTag()){
			msg.addTimetagArg(arg->AsTimeTagUnchecked());
		}
		else if(arg->IsRgbaColor()){
			msg.addRgbaColorArg(arg->AsRgbaColorUnchecked());
		}
		else if(arg->IsBlob()){
			const char * dataPtr;
			osc::osc_bundle_element_size_t len = 0;
			arg->AsBlobUnchecked((const void*&)dataPtr, len);
			ofBuffer buffer(dataPtr, len);
			msg.addBlobArg(buffer);
		}
		else {
			ofLogError("ofxOscReceiver") << "ProcessMessage(): argument in message "
				<< m.AddressPattern() << " is an unknown type "
				<< (int) arg->TypeTag() << " '" << (char) arg->TypeTag() << "'";
				break;
		}
	}

	// send msg to main thread
	messagesChannel.send(std::move(msg));
}

// friend functions
//--------------------------------------------------------------
std::ostream& operator<<(std::ostream &os, const ofxOscReceiver &receiver) {
	os << receiver.getPort();
	if(receiver.isListening()) {
		os << " listening";
	}
	return os;
}
