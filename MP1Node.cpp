/**********************************
 * FILE NAME: MP1Node.cpp
 *
 * DESCRIPTION: Membership protocol run by this Node.
 * 				Definition of MP1Node class functions.
 **********************************/

#include "MP1Node.h"
#include <sstream>
#include <time.h>

using namespace std;

/**********************************
 * 
 * My Modified Code 
 * 
 **********************************/

/**
 * FUNCTION NAME: initThisNode
 *
 * DESCRIPTION: Find out who I am and start up
 */
int MP1Node::initThisNode(Address *joinaddr) {
	/*
	 * This function is partially implemented and may require changes
	 */
    int id = *(int*)(&memberNode->addr.addr);
	int port = *(short*)(&memberNode->addr.addr[4]);


	memberNode->bFailed = false;
	memberNode->inited = true;
	memberNode->inGroup = false;
    // node is up!
	memberNode->nnb = 0;
	memberNode->heartbeat = 0;
	memberNode->pingCounter = TFAIL;
	memberNode->timeOutCounter = -1;
    initMemberListTable(memberNode,id,port);

    stringstream ss;
    cout << "initThisNode: id=" << id << "| port=" << port << endl;

    return 0;
}

/**
 * FUNCTION NAME: recvCallBack
 *
 * DESCRIPTION: Message handler for different message types
 */
bool MP1Node::recvCallBack(void *envelope, char *data, int size ) {
		
    /*
     * the envelope is a pointer to a Message struct, containing:
            Message Header
            An Address
            An Optional Member Table
    */

    Message* message = (Message*) data;

    switch (message->messageType) {
            case JOINREQ:
                    handleJoinRequest(message);
                    break;
            case JOINREP:
                    handleJoinReply(message);
                    break;
            case MEMBER_TABLE:
                    handleMemberTable(message);
                    break;
            default:
                    log->LOG(&memberNode->addr, "Received other msg");
                    break;
    }
    return 0;
}

/**
 * FUNCTION NAME: nodeLoopOps
 *
 * DESCRIPTION: Check if any node hasn't responded within a timeout period and then delete
 * 				the nodes
 * 				Propagate your membership list
 */
void MP1Node::nodeLoopOps() {

    // set timeout value
    int timeout = 5;

    //increment heartbeat
    memberNode->heartbeat += 1;

    //check for failed nodes and delete them
    for (vector<MemberListEntry>::iterator it = memberNode->memberList.begin();
         it != memberNode->memberList.end();) {
        if (par->getcurrtime() - it->timestamp > timeout) {
            Address addr = makeAddress(it->id, it->port);
            cout  << "Timing out " << addr.getAddress() << endl;
            log->logNodeRemove(&memberNode->addr, &addr);
            memberNode->memberList.erase(it);
        } else {
            it++;
        }
    }

    //increment the heartbeat
    memberNode->heartbeat += 1;

    //now update informatino for this node
    int id = getAddressId(memberNode->addr);
    short port = getAddressPort(memberNode->addr);
    updateMemberList(id, port, memberNode->heartbeat);

    //send some member tables out to peers
    sendMemberTables(id, port, memberNode->heartbeat);

    //send out member table messages to a few folks
    return;
}

/**
 * FUNCTION NAME: finishUpThisNode
 *
 * DESCRIPTION: Wind up this node and clean up state
 */
int MP1Node::finishUpThisNode(){
   /*
    * Your code goes here
    */
    return 0;
}

/*
 * Note: You can change/add any functions in MP1Node.{h,cpp}
 */

/**********************************  
 * 
 * Lib calls and functions
 * 
 **********************************/

/**
 * Overloaded Constructor of the MP1Node class
 * You can add new members to the class if you think it
 * is necessary for your logic to work
 */
MP1Node::MP1Node(Member *member, Params *params, EmulNet *emul, Log *log, Address *address) {
	for( int i = 0; i < 6; i++ ) {
		NULLADDR[i] = 0;
	}
	this->memberNode = member;
	this->emulNet = emul;
	this->log = log;
	this->par = params;
	this->memberNode->addr = *address;
}

/**
 * Destructor of the MP1Node class
 */
MP1Node::~MP1Node() {}

/**
 * FUNCTION NAME: recvLoop
 *
 * DESCRIPTION: This function receives message from the network and pushes into the queue
 * 				This function is called by a node to receive messages currently waiting for it
 */
int MP1Node::recvLoop() {
    if ( memberNode->bFailed ) {
    	return false;
    }
    else {
    	return emulNet->ENrecv(&(memberNode->addr), enqueueWrapper, NULL, 1, &(memberNode->mp1q));
    }
}

/**
 * FUNCTION NAME: enqueueWrapper
 *
 * DESCRIPTION: Enqueue the message from Emulnet into the queue
 */
int MP1Node::enqueueWrapper(void *env, char *buff, int size) {
	Queue q;
	return q.enqueue((queue<q_elt> *)env, (void *)buff, size);
}

/**
 * FUNCTION NAME: nodeStart
 *
 * DESCRIPTION: This function bootstraps the node
 * 				All initializations routines for a member.
 * 				Called by the application layer.
 */
void MP1Node::nodeStart(char *servaddrstr, short servport) {
    Address joinaddr;
    joinaddr = getJoinAddress();

    cout << "nodeStart: " << this->memberNode->addr.getAddress() << " joinaddr=" << joinaddr.getAddress() << endl;

    // Self booting routines
    if( initThisNode(&joinaddr) == -1 ) {
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "init_thisnode failed. Exit.");
#endif
        exit(1);
    }

    if( !introduceSelfToGroup(&joinaddr) ) {
        finishUpThisNode();
#ifdef DEBUGLOG
        log->LOG(&memberNode->addr, "Unable to join self to group. Exiting.");
#endif
        exit(1);
    }

    return;
}



/**
 * FUNCTION NAME: introduceSelfToGroup
 *
 * DESCRIPTION: Join the distributed system
 */
int MP1Node::introduceSelfToGroup(Address *joinaddr) {
    Message *msg;
#ifdef DEBUGLOG
    static char s[1024];
#endif

    //message always has the address of the sender

    if ( 0 == memcmp((char *)&(memberNode->addr.addr), (char *)&(joinaddr->addr), sizeof(memberNode->addr.addr))) {
        // I am the group booter (first process to join the group). Boot up the group
#ifdef DEBUGLOG        
        log->LOG(&memberNode->addr, "Starting up group...");
#endif
        memberNode->inGroup = true;
        int id = getAddressId((Address) *joinaddr);
        short port = getAddressPort((Address) *joinaddr);
        updateMemberList(id, port ,memberNode->heartbeat);
    }
    else {

        msg = new Message;
        size_t msgsize = sizeof(Message);

        // create JOINREQ message: format of data is {struct Address myaddr}
        msg->messageType = JOINREQ;

        // copy the address string from the requestor to the mesage
        //memcpy((char *)&(msg->address), (char *)&(membernode->addr), sizeof(msg->address));

        msg->address = memberNode->addr;
        cout << "introduceSelfToGroup:member address is " << memberNode->addr.getAddress() << endl;
        msg->heartbeat = memberNode->heartbeat;

#ifdef DEBUGLOG
        sprintf(s, "Trying to join...");
        log->LOG(&memberNode->addr, s);
#endif

        // send JOINREQ message to introducer member
        cout << "introduceSelfToGroup:sending Join Mesage" << endl;
        emulNet->ENsend(&memberNode->addr, joinaddr, (char *)msg, msgsize);        

        free(msg);
    }

    return 1;

}


/**
 * FUNCTION NAME: nodeLoop
 *
 * DESCRIPTION: Executed periodically at each member
 * 				Check your messages in queue and perform membership protocol duties
 */
void MP1Node::nodeLoop() {
    if (memberNode->bFailed) {
    	return;
    }
    int id = *(int*)(&memberNode->addr.addr);
    //cout << "nodeLoop for id" << id << ": inGroup=" << memberNode->inGroup << endl;

    // Check my messages
    checkMessages();

    // Wait until you're in the group...
    if( !memberNode->inGroup ) {
    	return;
    }

    // ...then jump in and share your responsibilites!
    nodeLoopOps();

    return;
}

/**
 * FUNCTION NAME: checkMessages
 *
 * DESCRIPTION: Check messages in the queue and call the respective message handler
 */
void MP1Node::checkMessages() {
    void *ptr;
    int size;

    // Pop waiting messages from memberNode's mp1q
    while ( !memberNode->mp1q.empty() ) {
    	ptr = memberNode->mp1q.front().elt;
    	size = memberNode->mp1q.front().size;
    	memberNode->mp1q.pop();
    	recvCallBack((void *)memberNode, (char *)ptr, size);
    }
    return;
}


/**
 * FUNCTION NAME: isNullAddress
 *
 * DESCRIPTION: Function checks if the address is NULL
 */
int MP1Node::isNullAddress(Address *addr) {
	return (memcmp(addr->addr, NULLADDR, 6) == 0 ? 1 : 0);
}

/**
 * FUNCTION NAME: getJoinAddress
 *
 * DESCRIPTION: Returns the Address of the coordinator
 */
Address MP1Node::getJoinAddress() {
    Address joinaddr;

    memset(&joinaddr, 0, sizeof(Address));
    *(int *)(&joinaddr.addr) = 1;
    *(short *)(&joinaddr.addr[4]) = 0;

    return joinaddr;
}

/**
 * FUNCTION NAME: initMemberListTable
 *
 * DESCRIPTION: Initialize the membership list
 */
void MP1Node::initMemberListTable(Member *memberNode, int id, short port) {
        memberNode->memberList.clear();
        MemberListEntry mle = MemberListEntry(id, port);
        mle.settimestamp(par->getcurrtime());
        mle.setheartbeat(memberNode->heartbeat);
        memberNode->memberList.push_back(mle);
}


/**
 * FUNCTION NAME: printAddress
 *
 * DESCRIPTION: Print the Address
 */
void MP1Node::printAddress(Address *addr)
{
    printf("%d.%d.%d.%d:%d \n",  
			addr->addr[0],
			addr->addr[1],
			addr->addr[2],
            addr->addr[3], 
            *(short*)&addr->addr[4]) ;    
}

void MP1Node::handleJoinRequest(Message *mRequest) {

        //create a join reply and send it
        Message *mReply;
        mReply = new Message;
        size_t msgsize = sizeof(Message);

        mReply->messageType = JOINREP;
        //copy the address - the message gets the FROM address stored in the message
        memcpy((char *)&(mReply->address), (char *)&(memberNode->addr), sizeof(memberNode->addr));
        mReply->heartbeat = memberNode->heartbeat;

        Address *requestor= new Address(mRequest->address);

        int id = getAddressId(*requestor);
        short port = getAddressPort(*requestor);
        updateMemberList(id, port, mRequest->heartbeat);

        logMemberStatus();

        cout << "handleJoinrequest:Sending JOINREP from ";
        cout << mRequest->address.getAddress();
        cout << "to " << memberNode;
        cout << ":heartbeat=" << memberNode->heartbeat << endl;

        emulNet->ENsend(&memberNode->addr, requestor, (char *)mReply, msgsize);
        free(mReply);
        free(mRequest);
    }

void MP1Node::handleJoinReply(Message *message) {

    //got a reply so we are in the group now
    memberNode->inGroup = true;

    // update the heartbeat and the member table
    Address sender = message->address;
    int id = getAddressId(sender);
    short port = getAddressPort(sender);
    updateMemberList(id, port, message->heartbeat);
    cout << "handleJoinReply: reply from " << sender.getAddress() << endl;
}



void MP1Node::handleMemberTable(Message *message) {

}

void MP1Node::logMemberStatus() {
    cout << "for node " << memberNode->addr.getAddress() << "==>" << endl;
    cout << "heartbeat = " << memberNode->heartbeat << endl;
    cout << "[";
    for (vector<MemberListEntry>::iterator it = memberNode->memberList.begin(); it != memberNode->memberList.end(); it++) {
        cout << it->getid() << ":" << it->getheartbeat() << ":" << it->gettimestamp() << ",";
    }
    cout << "]" << endl;
}

void MP1Node::updateMemberList(int id, short port, long heartbeat)  {

        vector<MemberListEntry>::iterator it = memberNode->memberList.begin();
        for (; it != memberNode->memberList.end(); ++it) {
            if (it->id == id && it->port ==port) {
                if (heartbeat > it->heartbeat) {
                    it->setheartbeat(heartbeat);
                    it->settimestamp(par->getcurrtime());
                }
                return;
            }
        }

        MemberListEntry memberEntry(id, port, heartbeat, par->getcurrtime());
        memberNode->memberList.push_back(memberEntry);

        Address joinaddr;
        memcpy(&joinaddr.addr[0], &id, sizeof(int));
        memcpy(&joinaddr.addr[4], &port, sizeof(short));
        log->logNodeAdd(&memberNode->addr, &joinaddr);

        cout << "updatememberlist: id=" << id << " port: " << port << endl;
        logMemberStatus();
}

Address MP1Node::makeAddress(int id, short port) {
    Address addr;
    memcpy(&addr.addr[0], &id, sizeof(int));
    memcpy(&addr.addr[4], &port, sizeof(short));
    return addr;
}

int MP1Node::getAddressId(Address myAddress) {
    int id;
    memcpy(&id, &myAddress.addr, sizeof(int));
    return id;
}

short MP1Node::getAddressPort(Address myAddress) {
    short port;
    memcpy(&port, &myAddress.addr[4], sizeof(short));
    return port;
}


void MP1Node::sendMemberTables(int id, short port, long heartbeat)
{

    Message *message;
    message = new Message;
    size_t messageSize = sizeof(Message);

    message->messageType = MEMBER_TABLE;

    //copy the address - the sent message gets assigned the FROM address
    memcpy((char *)&(message->address), (char *)&(memberNode->addr), sizeof(memberNode->addr));

    //copy the menber list table
    message->memberList = memberNode->memberList;\

        //copy the heartbeat
        message->heartbeat = memberNode->heartbeat;

    for (vector<MemberListEntry>::iterator it = memberNode->memberList.begin(); it != memberNode->memberList.end(); it++) {
        Address destination = makeAddress(it->id, it->port);

        srand(time(0)); // use current time as seed for random generator

        //loop through all nodes except this address
        if ((it->id == id) && (it->port == port) == 0) {
            long random = rand();
            if ((random  % INFECTIOUSNESS)==0) {
                cout << "random =" << random << endl;
                cout << "inf" << INFECTIOUSNESS << endl;
                cout << random  % INFECTIOUSNESS;
                emulNet->ENsend(&memberNode->addr, &destination, (char *)message, messageSize);
            }
        }
    }
}
