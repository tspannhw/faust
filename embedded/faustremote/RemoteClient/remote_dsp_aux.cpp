/************************************************************************
 ************************************************************************
 FAUST compiler
 Copyright (C) 2003-2013 GRAME, Centre National de Creation Musicale
 ---------------------------------------------------------------------
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/

#include <sstream>
#include "remote_dsp_aux.h"
#include "faust/gui/ControlUI.h"
#include "faust/dsp/llvm-dsp.h"
#include "utilities.h"
#include <errno.h>
#include <string.h>
#include <libgen.h>

FactoryTableType remote_dsp_factory::gFactoryTable;
static CURL* gCurl = NULL;

// Standard Callback to store a server response in stringstream
static size_t storeResponse(void *buf, size_t size, size_t nmemb, void* userp)
{
    ostream* os = static_cast<ostream*>(userp);
    streamsize len = size * nmemb;
    return (os->write(static_cast<char*>(buf), len)) ? len : 0;
}

static bool isInteger(const string& str)
{
    for (size_t i = 0; i < str.size(); i++) {
        if (!isdigit(str[i])) {
            return false;
        }
    }
    return true;
}

//Returns true if no problem encountered
//The response string stores the data received 
//(can be error or real data... depending on return value)
//The errorCode stores the error encoded as INT
static bool sendRequest(const string& url, const string& finalRequest, string& response, int& errorCode)
{
    bool res = false;
        
    printf("Request = %s and url = %s\n", finalRequest.c_str(), url.c_str());
    
    ostringstream oss;
    curl_easy_setopt(gCurl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(gCurl, CURLOPT_POST, 1L);
    curl_easy_setopt(gCurl, CURLOPT_POSTFIELDSIZE, (long)(finalRequest.size()));
    curl_easy_setopt(gCurl, CURLOPT_POSTFIELDS, finalRequest.c_str());
    curl_easy_setopt(gCurl, CURLOPT_WRITEFUNCTION, &storeResponse);
    curl_easy_setopt(gCurl, CURLOPT_FILE, &oss);
    curl_easy_setopt(gCurl, CURLOPT_CONNECTTIMEOUT, 15); 
    curl_easy_setopt(gCurl, CURLOPT_TIMEOUT, 15);
    
    if (curl_easy_perform(gCurl) != CURLE_OK) {
        printf("Easy perform error\n");
        errorCode = ERROR_CURL_CONNECTION;
    } else {
        
        long respcode; //response code of the http transaction
        curl_easy_getinfo(gCurl, CURLINFO_RESPONSE_CODE, &respcode);
        
        if (respcode == 200) {
            response = oss.str();
            res = true;
        } else if (respcode == 400) {
            printf("Info Failed\n");
            if (isInteger(oss.str())) {
                errorCode = atoi(oss.str().c_str());
            } else {
                response = oss.str();
            }
        }
    }
     
    return res;
}

//------------------FACTORY

// Init remote dsp factory sends a POST request to a remote server
// The URL extension used is /GetJson
// The datas have a url-encoded form (key/value separated by & and special character are reencoded like spaces = %)
bool remote_dsp_factory::init(int argc, const char *argv[], 
                            const string& ip_server, 
                            int port_server, 
                            const string& name_app, 
                            const string& dsp_content, 
                            const string& sha_key, 
                            string& error, 
                            int opt_level)
{
    bool res = false;
    int errorCode;
    string response, ip;
    stringstream finalRequest, serverIP;
    
    fSHAKey = sha_key;
     
    // Adding Compilation Options to request data
    finalRequest << "name=" << name_app << "&number_options=" << argc;
    for (int i = 0; i < argc; i++) {
        finalRequest << "&options=" << argv[i];
    }
    
    // Adding LLVM optimization Level to request data
    finalRequest << "&opt_level=" << opt_level << "&shaKey=" << fSHAKey;
           
    // Compile locally and send machine code on server side...
    if (isopt(argc, argv, "-machine")) {
        string error;
        llvm_dsp_factory* factory = createDSPFactoryFromString(name_app, dsp_content, 
                                                            argc, argv, 
                                                            loptions(argv,"-machine", ""), 
                                                            error, 3);
        if (factory) {
            // Transforming machine code to URL format
            string machine_code = writeDSPFactoryToMachine(factory);
            finalRequest << "&dsp_data=";
            finalRequest << curl_easy_escape(gCurl, machine_code.c_str(), machine_code.size());
            deleteDSPFactory(factory);
        } else {
            printf("Compilation error : %s\n", error.c_str());
            return res;
        }
    } else {
        // Transforming DSP code to URL format
        finalRequest << "&dsp_data=";
        finalRequest << curl_easy_escape(gCurl, dsp_content.c_str(), dsp_content.size());
        
        // Keep library path
        stringstream os(dsp_content);
        string key, name;                 
        while (os >> key) {               
            os >> name;
            if (key == "compilation_options") {
                break;
            } else if (key == "library_path") {
                fPathnameList.push_back(name);
            }  
        }
    }
    
    serverIP << "http://" << ip_server << ":" << port_server;
    fServerIP = serverIP.str();
    ip = fServerIP + "/GetJson";
   
    errorCode = -1;
    if (sendRequest(ip, finalRequest.str(), response, errorCode)) {
        decodeJson(response);
        res = true;
    } else if (errorCode != -1) {
        error = "Curl Connection Failed";
    } else {
        error = response;
    }
    
    return res;
}

// Delete remote dsp factory sends an explicit delete request to server
void remote_dsp_factory::stop() 
{
    // The index of the factory to delete has to be sent
    string finalRequest = "shaKey=" + fSHAKey;
    string url = fServerIP + "/DeleteFactory";
   
    string response;
    int errorCode;
   
    if (!sendRequest(url, finalRequest, response, errorCode)) {
        printf("sendRequest failed: %s || code %i\n", response.c_str(), errorCode);
    }
}

// Decoding JSON from a string to
// fUiItems : Structure containing the graphical items
// fMetadatas : Structure containing the metadatas
// Some "extra" metadatas are kept separatly
void remote_dsp_factory::decodeJson(const string& json)
{
    const char* p = json.c_str();
    parseJson(p, fMetadatas, fUiItems);
    
    fNumInputs = atoi(fMetadatas["inputs"].c_str());
    fMetadatas.erase("inputs");
    
    fNumOutputs = atoi(fMetadatas["outputs"].c_str());
    fMetadatas.erase("outputs");
}

// Declaring meta datas
void remote_dsp_factory::metadataRemoteDSPFactory(Meta* m) 
{ 
    map<string,string>::iterator it;
    for (it = fMetadatas.begin() ; it != fMetadatas.end(); it++) {
        m->declare(it->first.c_str(), it->second.c_str());
    }
}   

// Create Remote DSP Instance from factory
remote_dsp_aux* remote_dsp_factory::createRemoteDSPInstance(int argc, const char *argv[], 
                                                            int sampling_rate, int buffer_size, 
                                                            RemoteDSPErrorCallback error_callback, 
                                                            void* error_callback_arg, 
                                                            int& error) 
{
    remote_dsp_aux* dsp = new remote_dsp_aux(this);
    if (dsp->init(argc, argv, sampling_rate, buffer_size, error_callback, error_callback_arg, error)) {
        return dsp; 
    } else {
        delete dsp;
        return NULL;
    }
}

//---------FACTORY

static bool getFactory(const string& sha_key, FactoryTableIt& res)
{
    FactoryTableIt it;
    
    for (it = remote_dsp_factory::gFactoryTable.begin(); it != remote_dsp_factory::gFactoryTable.end(); it++) {
        FactoryTableItem val = (*it).second;
        if (val.first == sha_key) {
            res = it;
            return true;
        }
    }
    
    return false;
}

//--------------------INSTANCES

remote_dsp_aux::remote_dsp_aux(remote_dsp_factory* factory)
{
    fFactory = factory;
    fNetJack = NULL;
    
    fAudioInputs = new float*[getNumInputs()];
    fAudioOutputs = new float*[getNumOutputs()];
    
    for (int i = 0; i < getNumInputs(); i++) {
        fAudioInputs[i] = 0;
    }
    for (int i = 0; i < getNumOutputs(); i++) {
        fAudioOutputs[i] = 0;
    }
    
    fControlInputs = new float*[1];
    fControlOutputs = new float*[1];
    
    fControlInputs[0] = 0;
    fControlOutputs[0] = 0;
    
    fCounterIn = 0;
    fCounterOut = 0;
    
    fErrorCallback = 0;
    fErrorCallbackArg = 0;
    
    fRunningFlag = true;
}
        
remote_dsp_aux::~remote_dsp_aux()
{
    if (fNetJack) {
    
        jack_net_master_close(fNetJack); 
        
        delete[] fInControl;
        delete[] fOutControl;
        
        delete[] fControlInputs[0];
        delete[] fControlOutputs[0];
    
        fNetJack = 0;
    }
    
    delete[] fAudioInputs;
    delete[] fAudioOutputs;
    
    delete[] fControlInputs;
    delete[] fControlOutputs;
}

void remote_dsp_aux::fillBufferWithZerosOffset(int channels, int offset, int size, FAUSTFLOAT** buffer)
{
    // Cleanup audio buffers only 
    for (int i = 0; i < channels; i++) {
        memset(&buffer[i][offset], 0, sizeof(float)*size);
    }
}

// Fonction for command line parsing
const char* remote_dsp_aux::getValueFromKey(int argc, const char *argv[], const char *key, const char* defaultValue)
{
    for (int i = 0; i < argc; i++){
        if (strcmp(argv[i], key) == 0) {
            return argv[i+1];   
        }
    }
	return defaultValue;
}

// Decode internal structure, to build user interface
void remote_dsp_aux::buildUserInterface(UI* ui) 
{
    vector<itemInfo*> jsonItems = fFactory->itemList();
    
    // To be sure the floats are correctly encoded
    char* tmp_local = setlocale(LC_ALL, NULL);
    setlocale(LC_ALL, "C");
    
    vector<itemInfo*>::iterator it;
    
    int counterIn = 0;
    int counterOut = 0;
    
    for (it = jsonItems.begin(); it != jsonItems.end() ; it++) {
        
        float init = strtod((*it)->init.c_str(), NULL);
        float min = strtod((*it)->min.c_str(), NULL);
        float max = strtod((*it)->max.c_str(), NULL);
        float step = strtod((*it)->step.c_str(), NULL);
        
        map<string,string>::iterator it2;
        
        bool isInItem = false;
        bool isOutItem = false;
        
        // Meta Data declaration for entry items
        if ((*it)->type.find("group") == string::npos && (*it)->type.find("bargraph") == string::npos && (*it)->type != "close") {
            
            fInControl[counterIn] = init;
            isInItem = true;
            
            for (it2 = (*it)->meta.begin(); it2 != (*it)->meta.end(); it2++)
                ui->declare(&fInControl[counterIn], it2->first.c_str(), it2->second.c_str());
        }
        // Meta Data declaration for exit items
        else if ((*it)->type.find("bargraph") != string::npos){
            
            fOutControl[counterOut] = init;
            isOutItem = true;
            
            for(it2 = (*it)->meta.begin(); it2 != (*it)->meta.end(); it2++){
                ui->declare(&fOutControl[counterOut], it2->first.c_str(), it2->second.c_str());
            }
        }
        // Meta Data declaration for group opening or closing
        else {
            for (it2 = (*it)->meta.begin(); it2 != (*it)->meta.end(); it2++)
                ui->declare(0, it2->first.c_str(), it2->second.c_str());
        }
        
        // Item declaration
        string type = (*it)->type;
        if (type == "hgroup") 
            ui->openHorizontalBox((*it)->label.c_str());
        
        else if (type == "vgroup") 
             ui->openVerticalBox((*it)->label.c_str());
     
        else if (type == "tgroup")
            ui->openTabBox((*it)->label.c_str());
        
        else if (type == "vslider")
            ui->addVerticalSlider((*it)->label.c_str(), &fInControl[counterIn], init, min, max, step);
        
        else if (type == "hslider")
            ui->addHorizontalSlider((*it)->label.c_str(), &fInControl[counterIn], init, min, max, step);            
        
        else if (type == "checkbox")
            ui->addCheckButton((*it)->label.c_str(), &fInControl[counterIn]);
        
        else if (type == "hbargraph")
            ui->addHorizontalBargraph((*it)->label.c_str(), &fOutControl[counterOut], min, max);
        
        else if (type == "vbargraph")
            ui->addVerticalBargraph((*it)->label.c_str(), &fOutControl[counterOut], min, max);
        
        else if (type == "nentry")
            ui->addNumEntry((*it)->label.c_str(), &fInControl[counterIn], init, min, max, step);
        
        else if (type == "button")
            ui->addButton((*it)->label.c_str(), &fInControl[counterIn]);
        
        else if (type == "close")
            ui->closeBox();
            
        if (isInItem)
            counterIn++;
            
        if (isOutItem)
            counterOut++;
    }
    
    // Keep them for compute method...
    fCounterIn = counterIn;
    fCounterOut = counterOut;
    
    setlocale(LC_ALL, tmp_local);
}

void remote_dsp_aux::setupBuffers(FAUSTFLOAT** input, FAUSTFLOAT** output, int offset)
{
    for (int i = 0; i < getNumInputs(); i++) {
        fAudioInputs[i] = &input[i][offset];
    }
    
    for (int i = 0; i < getNumOutputs(); i++) {
        fAudioOutputs[i] = &output[i][offset];
    }
}

void remote_dsp_aux::sendSlice(int buffer_size) 
{
    if (fRunningFlag && jack_net_master_send_slice(fNetJack, getNumInputs(), fAudioInputs, 1, (void**)fControlInputs, buffer_size) < 0) {
        fillBufferWithZerosOffset(getNumOutputs(), 0, buffer_size, fAudioOutputs);
        if (fErrorCallback) {
            fRunningFlag = (fErrorCallback(ERROR_NETJACK_WRITE, fErrorCallbackArg) == 0);
        }
    }
}

void remote_dsp_aux::recvSlice(int buffer_size)
{
    if (fRunningFlag && jack_net_master_recv_slice(fNetJack, getNumOutputs(), fAudioOutputs, 1, (void**)fControlOutputs, buffer_size) < 0) {
        fillBufferWithZerosOffset(getNumOutputs(), 0, buffer_size, fAudioOutputs);
        if (fErrorCallback) {
            fRunningFlag = (fErrorCallback(ERROR_NETJACK_READ, fErrorCallbackArg) == 0);
        }
    }
}

// Compute of the DSP, adding the controls to the input/output passed
void remote_dsp_aux::compute(int count, FAUSTFLOAT** input, FAUSTFLOAT** output)
{
    if (fRunningFlag) {
        
        // If count > fBufferSize : the cycle is divided in numberOfCycles NetJack cycles, and a lastCycle one
        int numberOfCycles = count/fBufferSize;
        int lastCycle = count%fBufferSize;
        
        int i = 0;
        for (i = 0; i < numberOfCycles; i++) {
            setupBuffers(input, output, i*fBufferSize);
            ControlUI::encode_midi_control(fControlInputs[0], fInControl, fCounterIn);
            sendSlice(fBufferSize);
            recvSlice(fBufferSize);
            ControlUI::decode_midi_control(fControlOutputs[0], fOutControl, fCounterOut);
        }
        
        if (lastCycle > 0) {
            setupBuffers(input, output, i*fBufferSize);
            ControlUI::encode_midi_control(fControlInputs[0], fInControl, fCounterIn);
            fillBufferWithZerosOffset(getNumInputs(), lastCycle, fBufferSize-lastCycle, fAudioInputs);
            sendSlice(lastCycle);
            recvSlice(lastCycle);
            ControlUI::decode_midi_control(fControlOutputs[0], fOutControl, fCounterOut);
        }
        
    } else {
        fillBufferWithZerosOffset(getNumOutputs(), 0, count, output);
    }
}

void remote_dsp_aux::metadata(Meta* m)
{ 
    fFactory->metadataRemoteDSPFactory(m);
}

// Accessors to number of input/output of DSP
int remote_dsp_aux::getNumInputs()
{ 
     return fFactory->getNumInputs();
}

int remote_dsp_aux::getNumOutputs()
{ 
    return fFactory->getNumOutputs();
}

// Useless fonction in our case but required for a DSP interface
//Interesting to implement one day ! 
void remote_dsp_aux::init(int /*sampling_rate*/) {}

// Init remote dsp instance sends a POST request to a remote server
// The URL extension used is /CreateInstance
// The datas to send are NetJack parameters & the factory index it is created from
// A NetJack master is created to open a connection with the slave opened on the server's side
bool remote_dsp_aux::init(int argc, const char* argv[], 
                        int sampling_rate, 
                        int buffer_size, 
                        RemoteDSPErrorCallback error_callback, 
                        void* error_callback_arg, 
                        int& error)
{
    
    fBufferSize = buffer_size;
    
    fErrorCallback = error_callback;
    fErrorCallbackArg = error_callback_arg;
     
    // Init Control Buffers
    fOutControl = new float[buffer_size];
    fInControl = new float[buffer_size];

    fControlInputs[0] = new float[8192];
    fControlOutputs[0] = new float[8192];
 
    memset(fOutControl, 0, sizeof(float)*buffer_size);
    memset(fInControl, 0, sizeof(float)*buffer_size);
    
    memset(fControlInputs[0], 0, sizeof(float)*8192);
    memset(fControlOutputs[0], 0, sizeof(float)*8192);
    
    // To be sure fCounterIn and fCounterOut are set before 'compute' is called, even if no 'buildUserInterface' is called by the client
    ControlUI dummy_ui;
    buildUserInterface(&dummy_ui);
    
    bool partial_cycle = atoi(getValueFromKey(argc, argv, "--NJ_partial", "0"));
    const char* port = getValueFromKey(argc, argv, "--NJ_port", "19000");
    
    // PREPARE URL TO SEND TO SERVER
    
    // Parse NetJack Parameters
    stringstream finalRequest;
    finalRequest << "NJ_ip=" << getValueFromKey(argc, argv, "--NJ_ip", searchIP().c_str());
    finalRequest << "&NJ_port=" << port;
    finalRequest << "&NJ_compression=" << getValueFromKey(argc, argv, "--NJ_compression", "-1");
    finalRequest << "&NJ_latency=" << getValueFromKey(argc, argv, "--NJ_latency", "2");
    finalRequest << "&NJ_mtu=" << getValueFromKey(argc, argv, "--NJ_mtu", "1500");
    finalRequest << "&factoryKey=" << fFactory->getKey();
    finalRequest << "&instanceKey=" << this;
    
    bool res = false;
    string url = fFactory->getIP();
    url += "/CreateInstance";
        
    string response;
    int errorCode = -1;

    // OPEN NET JACK CONNECTION
    if (sendRequest(url, finalRequest.str(), response, errorCode)) {
        jack_master_t request = { -1, -1, -1, -1, static_cast<jack_nframes_t>(buffer_size), static_cast<jack_nframes_t>(sampling_rate), "net_master", 5, partial_cycle};
        jack_slave_t result;
        fNetJack = jack_net_master_open(DEFAULT_MULTICAST_IP, atoi(port), &request, &result); 
        
        if (fNetJack) {
            res = true;
        } else {
            error = ERROR_NETJACK_NOTSTARTED;
        }
    } else {
        error = errorCode;
    }
    
    return res;
}                        

bool remote_dsp_aux::startAudio()
{
    stringstream finalRequest;
    string response;
    int errorCode;
    
    finalRequest << "instanceKey=" << this;
    string ip = fFactory->getIP() + "/StartAudio";
   
    return sendRequest(ip, finalRequest.str(), response, errorCode);
}

bool remote_dsp_aux::stopAudio()
{
    stringstream finalRequest;
    string response;
    int errorCode;
    
    finalRequest << "instanceKey=" << this;
    string ip = fFactory->getIP() + "/StopAudio";
      
    return sendRequest(ip, finalRequest.str(), response, errorCode);
}

//------ DISCOVERY OF AVAILABLE MACHINES
static remote_DNS* gDNS = NULL;

__attribute__((constructor)) static void initialize_libfaustremote() 
{
    gDNS = new remote_DNS();
    gCurl = curl_easy_init();
    if (!gCurl) {
        cout << "curl_easy_init error..." << endl;
    }
}

__attribute__((destructor)) static void destroy_libfaustremote()
{
    delete gDNS;
    curl_easy_cleanup(gCurl);
}

remote_DNS::remote_DNS()
{
    /* start a new server on port 7770 */
    fLoThread = lo_server_thread_new_multicast("224.0.0.1", "7770", remote_DNS::errorHandler);
    
    /* add method that will match the path /foo/bar, with two numbers, coerced to float and int */
    lo_server_thread_add_method(fLoThread, "/faustcompiler", "is", remote_DNS::pingHandler, this);
     
    lo_server_thread_start(fLoThread);
}
                                
remote_DNS::~remote_DNS()
{
    lo_server_thread_free(fLoThread);
}

void remote_DNS::errorHandler(int num, const char *msg, const char *path)
{
    printf("liblo server error %d in path %s: %s\n", num, path, msg);
}

/* catch any incoming messages and display them. returning 1 means that the
 * message has not been fully handled and the server should try other methods */
int remote_DNS::pingHandler(const char* path, const char* types, 
                            lo_arg** argv,
                            int argc, void* data, 
                            void* user_data)
{
    member messageSender;
    messageSender.pid = argv[0]->i;
    messageSender.hostname = (char*)argv[1];
    lo_timetag_now(&messageSender.timetag);
    stringstream convert;
    convert << messageSender.hostname  << ":" << messageSender.pid;
    
    if (gDNS->fLocker.Lock()) {
        gDNS->fClients[convert.str()] = messageSender;
        gDNS->fLocker.Unlock();
    }
        
    return 0;
}

//----------------------------------REMOTE DSP PUBLIC API-------------------------------------------

// FACTORIES

EXPORT remote_dsp_factory* getRemoteDSPFactoryFromSHAKey(const string& ip_server, int port_server, const string& sha_key)
{
    FactoryTableIt it;
    
    /// If available in the local cache, get it
    if (getFactory(sha_key, it)) {
        Sremote_dsp_factory sfactory = (*it).first;
        sfactory->addReference();
        return sfactory;
    } else {
    
        // Call server side to get remote factory, create local proxy factory, put it in the cache
        stringstream finalRequest;
        finalRequest << "shaKey=" << sha_key;
        
        stringstream serverIP;
        serverIP << "http://" << ip_server << ":" << port_server;
        string url = serverIP.str() + "/GetJsonFromKey";
         
        string response;
        int errorCode = -1;
        
        if (sendRequest(url, finalRequest.str(), response, errorCode)) {
            remote_dsp_factory* factory = new remote_dsp_factory();
            factory->setKey(sha_key);
            factory->setIP(serverIP.str());
            factory->decodeJson(response);
            remote_dsp_factory::gFactoryTable[factory] = make_pair(sha_key, list<remote_dsp_aux*>());
            return factory;
        } else {
            return NULL;
        }
    }
}

EXPORT remote_dsp_factory* createRemoteDSPFactoryFromFile(const string& filename, 
                                                        int argc, 
                                                        const char *argv[],
                                                        const string& ip_server, 
                                                        int port_server, 
                                                        string& error_msg, 
                                                        int opt_level)
{
    string base = basename((char*)filename.c_str());
    int pos = base.find(".dsp");
      
    if (pos != string::npos) {
        return createRemoteDSPFactoryFromString(base.substr(0, pos), pathToContent(filename), 
                                                argc, argv, 
                                                ip_server, 
                                                port_server,
                                                error_msg, 
                                                opt_level);
    } else {
        error_msg = "File Extension is not the one expected (.dsp expected)\n";
        return NULL;
    }
}

EXPORT remote_dsp_factory* createRemoteDSPFactoryFromString(const string& name_app, 
                                                            const string& dsp_content, 
                                                            int argc, 
                                                            const char* argv[], 
                                                            const string& ip_server, 
                                                            int port_server, 
                                                            string& error_msg, 
                                                            int opt_level)
{
    // Compute SHA1 key using the non-expanded version, IP and port
    stringstream sha_content;
    sha_content << dsp_content << ":" <<ip_server << ":" << port_server;
    
    string sha_key = generateSHA1(sha_content.str()); 
    FactoryTableIt it;
    
    vector<pair<string, string> > factories_list;
    getRemoteFactoriesAvailable(ip_server, port_server, &factories_list);
     
    bool stillExisting = false;
    for (int i = 0; i < factories_list.size(); i++) {
        if (sha_key == factories_list[i].second.c_str()) {
            stillExisting = true;
            break;
        }
    }
    
    if (getFactory(sha_key, it) && stillExisting) {
        Sremote_dsp_factory sfactory = (*it).first;
        sfactory->addReference();
        return sfactory;
    } else {
    
        // Use for it's possible 'side effects', that is generating SVG, XML... files
        string error_msg_aux;
        generateAuxFilesFromString(name_app, dsp_content, argc, argv, error_msg_aux);
        
        // OPTIONS have to be filtered for documentation not to be created on the server side (-tg, -sg, -ps, -svg, -mdoc, -xml)
        int argc1 = 0;
        const char* argv1[argc];
        
        for (int i = 0; i < argc; i++) {
            if (strcmp(argv[i],"-tg") != 0 && 
               strcmp(argv[i],"-sg") != 0 &&
               strcmp(argv[i],"-svg") != 0 &&
               strcmp(argv[i],"-ps") != 0 &&
               strcmp(argv[i],"-mdoc") != 0 &&
               strcmp(argv[i],"-xml") != 0)
            {
                argv1[argc1++] = argv[i];
            }
        }

        string sha_key_aux;
        string expanded_dsp = expandDSPFromString(name_app, dsp_content, argc1, argv1, sha_key_aux, error_msg_aux);
        
        if (expanded_dsp == "") {
            return 0; 
        }
    
        remote_dsp_factory* factory = new remote_dsp_factory();
        if (factory->init(argc1, argv1, ip_server, port_server, name_app, expanded_dsp, sha_key, error_msg, opt_level)) {
            remote_dsp_factory::gFactoryTable[factory] = make_pair(sha_key, list<remote_dsp_aux*>());
            return factory;
        } else {
            delete factory;
            return 0;
        }
    }
}

EXPORT void deleteRemoteDSPFactory(remote_dsp_factory* factory)
{
 //    
//    FactoryTableIt it;
//    if ((it = remote_dsp_factory::gFactoryTable.find(factory)) != remote_dsp_factory::gFactoryTable.end()) {
//        Sremote_dsp_factory sfactory = (*it).first;
//        if (sfactory->refs() == 2) { // Local stack pointer + the one in gFactoryTable...
//            // Last use, remove from the global table, pointer will be deleted
//            remote_dsp_factory::gFactoryTable.erase(factory);
//        } else {
//            sfactory->removeReference();
//        }
//    }

    // TO CHECK
    factory->stop();
    
//    
//    string finalRequest = "shaKey="+factory->getKey();
//    
//    string response;
//    int errorCode;
//    if(sendRequest("http://192.168.1.174:7777/DeleteFactory", finalRequest, response, errorCode))
//        printf("Factory Well Well deleted\n");
}

EXPORT void deleteAllRemoteDSPFactories()
{
    FactoryTableIt it;
    for (it = remote_dsp_factory::gFactoryTable.begin(); it != remote_dsp_factory::gFactoryTable.end(); it++) {
        // Decrement counter up to one...
        while (((*it).first)->refs() > 1) { ((*it).first)->removeReference(); }
    }
    // Then clear the table thus finally deleting all ref = 1 smart pointers
    remote_dsp_factory::gFactoryTable.clear();
}

EXPORT void metadataRemoteDSPFactory(remote_dsp_factory* factory, Meta* m)
{
    factory->metadataRemoteDSPFactory(m);
}

EXPORT vector<string> getLibraryList(remote_dsp_factory* factory) 
{ 
    return factory->getLibraryList(); 
}

EXPORT int remote_dsp_factory::getNumInputs() { return fNumInputs; }
EXPORT int remote_dsp_factory::getNumOutputs() { return fNumOutputs; }

EXPORT bool getRemoteMachinesAvailable(map<string, pair<string, int> >* machineList)
{
    if (gDNS && gDNS->fLocker.Lock()) {
        
        for (map<string, remote_DNS::member>::iterator it = gDNS->fClients.begin(); it != gDNS->fClients.end(); it++){
            
            remote_DNS::member iterMem = it->second;
            
            lo_timetag now;
            lo_timetag_now(&now);
            
            // If the server machine did not send a message for 3 secondes, it is considered disconnected
            if ((now.sec - iterMem.timetag.sec) < 3) {
                
                // Decompose HostName to have Name, Ip and Port of service
                string serviceNameCpy(iterMem.hostname);
                
                int pos = serviceNameCpy.find("._");
                string remainingString = serviceNameCpy.substr(pos+2, string::npos);
                pos = remainingString.find("._");
                string serviceIP = remainingString.substr(0, pos);
                string hostName = remainingString.substr(pos+2, string::npos);
                
                int pos2 = serviceIP.find(":");
                string ipAddr = serviceIP.substr(0, pos2);
                string port = serviceIP.substr(pos2+1, string::npos);
                
                (*machineList)[hostName] = make_pair(ipAddr, atoi(port.c_str()));
            }
        }
        
        gDNS->fLocker.Unlock();
        return true;
    } else {
       return false; 
    }
}

EXPORT bool getRemoteFactoriesAvailable(const string& ip_server, int port_server, vector<pair<string, string> >* factories_list)
{
    bool res = false;
        
    stringstream serverIP;
    serverIP << "http://" << ip_server << ":" << port_server << "/GetAvailableFactories";
   
    ostringstream oss;
    curl_easy_setopt(gCurl, CURLOPT_URL, serverIP.str().c_str());
    curl_easy_setopt(gCurl, CURLOPT_POST, 0L);
    curl_easy_setopt(gCurl, CURLOPT_WRITEFUNCTION, &storeResponse);
    curl_easy_setopt(gCurl, CURLOPT_FILE, &oss);
    curl_easy_setopt(gCurl, CURLOPT_CONNECTTIMEOUT, 15); 
    curl_easy_setopt(gCurl, CURLOPT_TIMEOUT, 15);
        
    if (curl_easy_perform(gCurl) == CURLE_OK) {
        
        long respcode; //response code of the http transaction
        curl_easy_getinfo(gCurl, CURLINFO_RESPONSE_CODE, &respcode);
        
        if (respcode == 200) {
            // PARSE RESPONSE TO EXTRACT KEY/VALUE
            string response = oss.str();
            stringstream os(response);   
            string name, key;   
            while (os >> key) {                
                os >> name;
                factories_list->push_back(make_pair(name, key));
            }
            res = true;
        } else if (respcode == 400) {
            printf("Info Failed\n");
        }
    } else {
        printf("Easy perform error\n");
    }
   
    return res;
}

//---------INSTANCES

EXPORT remote_dsp* createRemoteDSPInstance(remote_dsp_factory* factory, 
                                        int argc, 
                                        const char *argv[], 
                                        int sampling_rate, 
                                        int buffer_size, 
                                        RemoteDSPErrorCallback error_callback, 
                                        void* error_callback_arg, 
                                        int& error)
{
    FactoryTableIt it;
    if ((it = remote_dsp_factory::gFactoryTable.find(factory)) != remote_dsp_factory::gFactoryTable.end()) {
        remote_dsp_aux* instance 
            = factory->createRemoteDSPInstance(argc, argv, sampling_rate, buffer_size, error_callback, error_callback_arg, error);
        (*it).second.second.push_back(instance);
        return reinterpret_cast<remote_dsp*>(instance);
    } else {
        return 0;
    }
}

EXPORT void deleteRemoteDSPInstance(remote_dsp* dsp)
{
    FactoryTableIt it;
    remote_dsp_aux* dsp_aux = reinterpret_cast<remote_dsp_aux*>(dsp);
    remote_dsp_factory* factory = dsp_aux->getFactory();
    
    it = remote_dsp_factory::gFactoryTable.find(factory);
    assert(it != remote_dsp_factory::gFactoryTable.end());
    (*it).second.second.remove(dsp_aux);
    
    delete dsp_aux; 
}

EXPORT void remote_dsp::metadata(Meta* m)
{
    reinterpret_cast<remote_dsp_aux*>(this)->metadata(m);
}

EXPORT int remote_dsp::getNumInputs()
{
    return reinterpret_cast<remote_dsp_aux*>(this)->getNumInputs();
}

EXPORT int remote_dsp::getNumOutputs()
{
    return reinterpret_cast<remote_dsp_aux*>(this)->getNumOutputs();
}

EXPORT void remote_dsp::init(int sampling_rate)
{
    reinterpret_cast<remote_dsp_aux*>(this)->init(sampling_rate);
}

EXPORT void remote_dsp::buildUserInterface(UI* interface)
{
    reinterpret_cast<remote_dsp_aux*>(this)->buildUserInterface(interface);
}

EXPORT void remote_dsp::compute(int count, FAUSTFLOAT** input, FAUSTFLOAT** output)
{
    reinterpret_cast<remote_dsp_aux*>(this)->compute(count, input, output);
}

EXPORT bool remote_dsp::startAudio()
{
    return reinterpret_cast<remote_dsp_aux*>(this)->startAudio();
}

EXPORT bool remote_dsp::stopAudio()
{
    return reinterpret_cast<remote_dsp_aux*>(this)->stopAudio();
}
