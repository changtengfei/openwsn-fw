/**
\brief Security operations defined by IEEE802.15.4e standard

\author Savio Sciancalepore <savio.sciancalepore@poliba.it>, April 2015.
\author Giuseppe Piro <giuseppe.piro@poliba.it>,
\author Gennaro Boggia <gennaro.boggia@poliba.it>,
\author Luigi Alfredo Grieco <alfredo.grieco@poliba.it>
*/

#include "IEEE802154_security.h"

//=============================define==========================================

//=========================== variables =======================================

security_vars_t security_vars;

//=========================== prototypes ======================================

void IEEE802154security_getFrameCounter(macFrameCounter_t reference,
                                        uint8_t*          array);
uint8_t IEEE802154security_authLengthChecking(uint8_t securityLevel); //it determines the length of the Authentication field(not standard)

uint8_t IEEE802154security_auxLengthChecking(uint8_t KeyIdMode,
                                             bool    frameCounterSuppression,
                                             uint8_t frameCounterSize); //it determines the length of the Auxiliary Security Header (not standard)

bool IEEE802154security_incomingKeyUsagePolicyChecking(m_keyDescriptor* keydesc,
                                                       uint8_t          frameType,
                                                       uint8_t          cfi);

bool IEEE802154security_incomingSecurityLevelChecking(m_securityLevelDescriptor* secLevDesc,
                                                      uint8_t                    secLevel,
                                                      bool                       exempt);

m_securityLevelDescriptor* IEEE802154security_securityLevelDescriptorLookup(uint8_t frameType,
                                                                            uint8_t cfi);

m_deviceDescriptor* IEEE802154security_deviceDescriptorLookup(open_addr_t*     address,
                                                              open_addr_t*     PANID,
                                                              m_keyDescriptor* keyDescriptor);

m_keyDescriptor* IEEE802154security_keyDescriptorLookup(uint8_t      KeyIdMode,
                                                        open_addr_t* keySource,
                                                        uint8_t      KeyIndex,
                                                        open_addr_t* DeviceAddress,
                                                        open_addr_t* panID,
                                                        uint8_t      frameType);

//=========================== admin ===========================================

/**
\brief Initialization of security tables and parameters.
*/
void IEEE802154security_init(){

	//Setting UP Phase

	//MASTER KEY: OpenWSN
	memset(&security_vars.M_k[0], 0, 16);
	security_vars.M_k[0] = 0x4e;
	security_vars.M_k[1] = 0x53;
	security_vars.M_k[2] = 0x57;
	security_vars.M_k[3] = 0x6e;
	security_vars.M_k[4] = 0x65;
	security_vars.M_k[5] = 0x70;
	security_vars.M_k[6] = 0x4f;

	//Initialization of the MAC Security Level Table
	uint8_t i;
	for(i=0; i<5; i++){
		security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].FrameType = i;
		security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].CommandFrameIdentifier = 0;
		security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].DeviceOverrideSecurityMinimum = FALSE;
		security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].AllowedSecurityLevels = 7;
		security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].SecurityMinimum = 7;
	}

	//Initialization of MAC KEY TABLE
	memset(&security_vars.MacKeyTable,
               0,
               sizeof(m_macKeyTable));

	//Initialization of MAC DEVICE TABLE
	memset(&security_vars.MacDeviceTable,
               0,
               sizeof(m_macDeviceTable));

	//Initialization of Frame Counter
	security_vars.m_macFrameCounterMode = 0x05; //0x04 or 0x05

}

//=========================== public ==========================================
/**
\brief Adding of Auxiliary Security Header to the IEEE802.15.4 MAC header
*/
void IEEE802154security_prependAuxiliarySecurityHeader(OpenQueueEntry_t* msg){

        bool frameCounterSuppression;
        uint8_t temp8b;
        open_addr_t* temp_keySource;

	frameCounterSuppression = 0; //the frame counter is carried in the frame, otherwise 1;

	//max length of MAC frames
	// length of authentication Tag
	msg->l2_authenticationLength = IEEE802154security_authLengthChecking(msg->l2_securityLevel);

	//length of auxiliary security header
	msg->l2_auxiliaryLength = IEEE802154security_auxLengthChecking(msg->l2_keyIdMode,
                                                                       frameCounterSuppression,
                                                                       security_vars.m_macFrameCounterMode); //length of Key ID field


	if((msg->length+msg->l2_auxiliaryLength+msg->l2_authenticationLength+2) >= 130 ){ //2 bytes of CRC, 130 MaxPHYPacketSize
		return;
	}

	//start setting the Auxiliary Security Header
	if(msg->l2_keyIdMode !=0){//if the KeyIdMode is zero, keyIndex and KeySource are omitted
		temp8b = msg->l2_keyIndex; //key index field

		packetfunctions_reserveHeaderSize(msg, sizeof(uint8_t));
		*((uint8_t*)(msg->payload)) = temp8b;
	}

	switch(msg->l2_keyIdMode){
	case 0: //no KeyIDMode field
		temp_keySource = &security_vars.m_macDefaultKeySource;
	    memcpy(&(msg->l2_keySource), temp_keySource, sizeof(open_addr_t));
		break;
	case 1:
		temp_keySource = &security_vars.m_macDefaultKeySource;
		packetfunctions_writeAddress(msg,temp_keySource,OW_LITTLE_ENDIAN);
		break;
	case 2: //keySource with 16b address
		temp_keySource = &msg->l2_keySource;
		packetfunctions_reserveHeaderSize(msg, sizeof(uint8_t));
		*((uint8_t*)(msg->payload)) = temp_keySource->addr_64b[6];

		packetfunctions_reserveHeaderSize(msg, sizeof(uint8_t));
		*((uint8_t*)(msg->payload)) = temp_keySource->addr_64b[7];
		break;
	case 3: //keySource with 64b address
		temp_keySource = &msg->l2_keySource;
		packetfunctions_writeAddress(msg,temp_keySource,OW_LITTLE_ENDIAN);
		break;
	default:
		return;
        }

        //Frame Counter
        //here I have to insert the ASN: I can only reserve the space and
        //save the pointer. The ASN will be added by activity_ti1OrR11 procedure

        // reserve space
        packetfunctions_reserveHeaderSize(msg,sizeof(macFrameCounter_t));

        // Keep a pointer to where the ASN will be
        // Note: the actual value of the current ASN will be written by the
        //    IEEE802.15.4e when transmitting
        msg->l2_ASNFrameCounter = msg->payload;

        //security control field
        packetfunctions_reserveHeaderSize(msg, sizeof(uint8_t));
 
        temp8b = 0;
        temp8b |= msg->l2_securityLevel << ASH_SCF_SECURITY_LEVEL;//3b
        temp8b |= msg->l2_keyIdMode << ASH_SCF_KEY_IDENTIFIER_MODE;//2b
        temp8b |= frameCounterSuppression << ASH_SCF_FRAME_CNT_MODE; //1b

        if(security_vars.m_macFrameCounterMode == 0x04){
	   temp8b |= 0 << ASH_SCF_FRAME_CNT_SIZE; //1b
        } else{
	   temp8b |= 1 << ASH_SCF_FRAME_CNT_SIZE; //1b
        }

        temp8b |= 0 << 1;//1b reserved
        *((uint8_t*)(msg->payload)) = temp8b;
}

/**
\brief Key searching and encryption/authentication operations.
*/
void IEEE802154security_outgoingFrameSecurity(OpenQueueEntry_t*   msg){

	bool frameCounterSuppression;
	m_keyDescriptor* keyDescriptor;
        uint8_t i;
        uint8_t j;
        uint8_t nonce[13];
	uint8_t key[16];

	frameCounterSuppression = 0; //the frame counter is carried in the frame, otherwise 1;

	//search for a key
	keyDescriptor = IEEE802154security_keyDescriptorLookup(msg->l2_keyIdMode,
                                            &msg->l2_keySource,
                                            msg->l2_keyIndex,
                                            &msg->l2_keySource,
                                            (idmanager_getMyID(ADDR_PANID)),
                                            msg->l2_frameType);

	if(keyDescriptor==NULL){
	    openserial_printInfo(COMPONENT_SECURITY,ERR_SECURITY,
                                (errorparameter_t)0,
                                (errorparameter_t)500);
            return;
	}

	for(j=0;j<16;j++){
           key[j] = keyDescriptor->key[j];
	}

	uint8_t vectASN[5];
	if(frameCounterSuppression == 0){//the frame Counter is carried in the frame
           ieee154e_getAsn(vectASN);//gets asn from mac layer.
           //save the frame counter of the current frame
           msg->l2_frameCounter.bytes0and1 = vectASN[0]+256*vectASN[1];
           msg->l2_frameCounter.bytes2and3 = vectASN[2]+256*vectASN[3];
           msg->l2_frameCounter.byte4 = vectASN[4];

           IEEE802154security_getFrameCounter(msg->l2_frameCounter,
                                              msg->l2_ASNFrameCounter);
	} //otherwise the frame counter is not in the frame

	memset(&nonce[0], 0, 13);

	//cryptographic block
	switch(msg->l2_keyIdMode){
           case 0:
           case 1:
              for(i=0; i<8; i++){
                 nonce[i] = security_vars.m_macDefaultKeySource.addr_64b[i];
              }
           break;
           case 2:
              for(i=0; i<2; i++){
                 nonce[i] = msg->l2_keySource.addr_16b[i];
              }
              for(i=2; i<8; i++){
	         nonce[i] = 0;
              }
           break;
           case 3:
              for(i=0; i<8; i++){
                 nonce[i] = msg->l2_keySource.addr_64b[i];
              }
           break;
        }

	for(i=0;i<5;i++){
		nonce[8+i] = vectASN[i];
	}

	//aData string
	memset(&msg->aData[0], 0, 128);
	msg->aData[0] = msg->length-msg->l2_length;
	memcpy(&msg->aData[1], &msg->payload[0], msg->length-msg->l2_length);

        //Encryption and/or authentication 
        CRYPTO_ENGINE.aes_ccms_enc(msg->aData,
                                   msg->length-msg->l2_length,
                                   msg->l2_payload,
                                   &msg->l2_length,
                                   nonce,
                                   2,
                                   key,
                                   msg->l2_authenticationLength);

	if(msg->l2_authenticationLength!=0){
           packetfunctions_reserveFooterSize(msg,msg->l2_authenticationLength);
	}

}

/**
\brief Parsing of IEEE802.15.4 Auxiliary Security Header.
*/
void IEEE802154security_retrieveAuxiliarySecurityHeader(OpenQueueEntry_t*      msg, 
                                                        ieee802154_header_iht* tempheader){

	uint8_t frameCnt_Suppression;
	uint8_t frameCnt_Size;
	uint8_t temp8b;
        uint8_t i;
        uint8_t receivedASN[5];
        open_addr_t* temp_addr;

	//c retrieve the Security Control field
	//1byte, Security Control Field

	temp8b = *((uint8_t*)(msg->payload)+tempheader->headerLength);

	msg->l2_securityLevel = (temp8b >> ASH_SCF_SECURITY_LEVEL)& 0x07;//3b

	msg->l2_authenticationLength = IEEE802154security_authLengthChecking(msg->l2_securityLevel);

	//retrieve the KeyIdMode field
	msg->l2_keyIdMode = (temp8b >> ASH_SCF_KEY_IDENTIFIER_MODE)& 0x03;//2b

	frameCnt_Suppression = (temp8b >> ASH_SCF_FRAME_CNT_MODE)& 0x01;//1b
	frameCnt_Size = (temp8b >> ASH_SCF_FRAME_CNT_SIZE)& 0x01;//1b

	tempheader->headerLength = tempheader->headerLength+1;

	//Frame Counter field, //l
	if(frameCnt_Suppression == 0){//the frame counter is here
		//the frame counter size is 5 bytes, because we have the ASN
		for(i=0;i<5;i++){
			receivedASN[i] = *((uint8_t*)(msg->payload)+tempheader->headerLength);
			tempheader->headerLength = tempheader->headerLength+1;
	}

		msg->l2_frameCounter.bytes0and1 = receivedASN[0]+256*receivedASN[1];
		msg->l2_frameCounter.bytes2and3 = receivedASN[2]+256*receivedASN[3];
		msg->l2_frameCounter.byte4 = receivedASN[4];
	}

        //retrieve the Key Identifier field
	switch(msg->l2_keyIdMode){
	case 0:
            //key is derived implicitly
	    temp_addr = &security_vars.m_macDefaultKeySource;
	    memcpy(&(msg->l2_keySource), temp_addr, sizeof(open_addr_t));
	break;
	case 2:
             packetfunctions_readAddress(((uint8_t*)(msg->payload)+tempheader->headerLength),
                                         ADDR_16B,
                                         &msg->l2_keySource,
                                         OW_LITTLE_ENDIAN);
             tempheader->headerLength = tempheader->headerLength+2;
        break;
	case 1:
	case 3:
         packetfunctions_readAddress(((uint8_t*)(msg->payload)+tempheader->headerLength),
						              ADDR_64B,
						              &msg->l2_keySource,
						              OW_LITTLE_ENDIAN);
	     tempheader->headerLength = tempheader->headerLength+8;
	break;
	default:
	     msg->l2_toDiscard = 1;
	     return;
	}

	//retrieve the KeyIndex
	if(msg->l2_keyIdMode != 0){
           temp8b = *((uint8_t*)(msg->payload)+tempheader->headerLength);
           msg->l2_keyIndex = (temp8b);
           tempheader->headerLength = tempheader->headerLength+1;
	} else {
		//key is derived implicitly
		msg->l2_keyIndex = 1;
	}

	//aData string
	memset(&msg->aData[0], 0, 128);
	msg->aData[0] = tempheader->headerLength;
	memcpy(&msg->aData[1], &msg->payload[0], tempheader->headerLength);

}

/**
\brief Identification of the key used to protect the frame and unsecuring operations.
*/
void IEEE802154security_incomingFrame(OpenQueueEntry_t*      msg){

	m_deviceDescriptor*        deviceDescriptor;
	m_keyDescriptor*           keyDescriptor;
	m_securityLevelDescriptor* securityLevelDescriptor;
        uint8_t nonce[13];
        uint8_t i;
        uint8_t myASN[5];

	//f key descriptor lookup
	keyDescriptor = IEEE802154security_keyDescriptorLookup(msg->l2_keyIdMode,
                                                               &msg->l2_keySource,
                                                               msg->l2_keyIndex,
                                                               &msg->l2_keySource,
                                                               idmanager_getMyID(ADDR_PANID),
                                                               msg->l2_frameType);

	if(keyDescriptor==NULL){
           msg->l2_toDiscard = 2; //can't find the key
           return;
	}

	//g device descriptor lookup
	deviceDescriptor = IEEE802154security_deviceDescriptorLookup(&msg->l2_keySource,
                                                                     idmanager_getMyID(ADDR_PANID),
                                                                     keyDescriptor);

	if(deviceDescriptor==NULL){
           msg->l2_toDiscard = 3;
           return;
	}

	//h Security Level lookup
	securityLevelDescriptor = IEEE802154security_securityLevelDescriptorLookup(msg->l2_frameType,
                                                                                   msg->commandFrameIdentifier);

	//i+j+k
	if(IEEE802154security_incomingSecurityLevelChecking(securityLevelDescriptor,
				         msg->l2_securityLevel,
				         deviceDescriptor->Exempt)==FALSE){
           msg->l2_toDiscard = 4; //security level not allowed
	}

	//l+m Anti-Replay - not needed for TSCH mode

	//n Control of key used
	if(IEEE802154security_incomingKeyUsagePolicyChecking(keyDescriptor,
                                                             msg->l2_frameType,
                                                             0
                                                             )  ==FALSE){
           msg->l2_toDiscard = 6; // improper key used
	}

	
	memset(&nonce[0], 0, 13);
	
	switch(msg->l2_keyIdMode){
          case 0:
          case 1:
             for(i=0; i<8; i++){
                nonce[i] = security_vars.m_macDefaultKeySource.addr_64b[i];
             }
          break;
          case 2:
             for(i=0; i<2; i++){
                nonce[i] = msg->l2_keySource.addr_16b[i];
	     }
             for(i=2; i<8; i++){
                nonce[i] = 0;
             }
          break;
          case 3:
             for(i=0; i<8; i++){
                nonce[i] = msg->l2_keySource.addr_64b[i];
             }
          break;
        }

	//Frame Counter (ASN)
	ieee154e_getAsn(myASN);
	for(i=0;i<5;i++){
		nonce[8+i] = myASN[i];
	}

	CRYPTO_ENGINE.aes_ccms_dec(msg->aData,
				   msg->aData[0],
				   msg->payload,
				   &msg->length,
				   nonce,
				   2,
				   keyDescriptor->key,
				   msg->l2_authenticationLength);

	//q save the frame counter
	deviceDescriptor->FrameCounter = msg->l2_frameCounter;

}

/**
\brief Initialization of shared key for the DAG Root.
*/

void IEEE802154security_DAGRoot_init(void){

	open_addr_t*  my;
        uint8_t j;
	my = idmanager_getMyID(ADDR_64B);

	//Creation of the KeyDescriptor
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyIdLookupList.KeyIdMode = 3;
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyIdLookupList.KeyIndex = 1;
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyIdLookupList.KeySource = *(my);
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyIdLookupList.Address = *(my);
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyIdLookupList.PANId = *(idmanager_getMyID(ADDR_PANID));
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyUsageList[1].FrameType = IEEE154_TYPE_DATA;
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyUsageList[0].FrameType = IEEE154_TYPE_ACK;
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyUsageList[2].FrameType = IEEE154_TYPE_BEACON;

	for(j=0;j<16;j++){
	    security_vars.MacKeyTable.KeyDescriptorElement[0].key[j] = security_vars.M_k[j];
	}

	security_vars.MacDeviceTable.DeviceDescriptorEntry[0].deviceAddress = *(my);
	security_vars.MacKeyTable.KeyDescriptorElement[0].DeviceTable = &security_vars.MacDeviceTable;
	security_vars.m_macDefaultKeySource.type = ADDR_64B;
	security_vars.m_macDefaultKeySource = *(my);
}

/**
\brief Initialization of shared key for nodes that are not the DAG Root.
*/
void IEEE802154security_ChildsInit(ieee802154_header_iht ieee802514_header){

	open_addr_t* src;
        uint8_t j;
	src= &ieee802514_header.src;
	security_vars.MacKeyTable.KeyDescriptorElement[1].KeyIdLookupList.KeyIdMode = 3;
	security_vars.MacKeyTable.KeyDescriptorElement[1].KeyIdLookupList.KeySource = *(src);
	security_vars.MacKeyTable.KeyDescriptorElement[1].KeyIdLookupList.PANId = ieee802514_header.panid;
	security_vars.MacKeyTable.KeyDescriptorElement[1].KeyIdLookupList.KeyIndex = 1;
	security_vars.MacKeyTable.KeyDescriptorElement[1].KeyIdLookupList.Address = *(src);
	security_vars.MacKeyTable.KeyDescriptorElement[1].KeyUsageList[1].FrameType = IEEE154_TYPE_DATA;
	security_vars.MacKeyTable.KeyDescriptorElement[1].KeyUsageList[0].FrameType = IEEE154_TYPE_ACK;
	security_vars.MacKeyTable.KeyDescriptorElement[0].KeyUsageList[2].FrameType = IEEE154_TYPE_BEACON;
	for(j=0;j<16;j++){
		security_vars.MacKeyTable.KeyDescriptorElement[1].key[j] = security_vars.M_k[j];
	}
	security_vars.m_macDefaultKeySource.type = ADDR_64B;
	security_vars.m_macDefaultKeySource = *(src);
	security_vars.MacDeviceTable.DeviceDescriptorEntry[1].deviceAddress = *(src);
	security_vars.MacDeviceTable.DeviceDescriptorEntry[1].FrameCounter.bytes0and1 = 0;
	security_vars.MacDeviceTable.DeviceDescriptorEntry[1].FrameCounter.bytes2and3 = 0;
	security_vars.MacKeyTable.KeyDescriptorElement[1].DeviceTable = &security_vars.MacDeviceTable;

	//this is necessary if multihop secure communications need to be estabilished
	IEEE802154security_DAGRoot_init();
}

//=========================== private =========================================

/**
\brief Identification of the length of the MIC.
*/
uint8_t IEEE802154security_authLengthChecking(uint8_t securityLevel){

	uint8_t authlen;

	switch (securityLevel) {
	 case 0 :
		 authlen = 0;
		 break;
	 case 1 :
		 authlen = 4;
		 break;
	 case 2 :
		 authlen = 8;
	 		 break;
	 case 3 :
		 authlen = 16;
	 		 break;
	 case 4 :
		 authlen = 0;
	 		 break;
	 case 5 :
		 authlen = 4;
	 		 break;
	 case 6 :
		 authlen = 8;
	 		 break;
	 case 7 :
		 authlen = 16;
	 		 break;
	}

	return authlen;

}

/**
\brief Length of the IEEE802.15.4 Auxiliary Security Header.
*/
uint8_t IEEE802154security_auxLengthChecking(uint8_t KeyIdMode, 
                                             bool frameCounterSuppression, 
                                             uint8_t frameCounterSize){

	uint8_t auxilary_len;
	uint8_t frameCntLength;
	if(frameCounterSuppression == 0){
		if(frameCounterSize == 0x04){
			frameCntLength = 4;
		} else{
			frameCntLength = 5;
		}
	} else{
		frameCntLength = 0;
	}

	switch(KeyIdMode){
		case 0:
			auxilary_len = frameCntLength + 1; //only SecLev and FrameCnt
			break;
		case 1:
			auxilary_len = frameCntLength + 1 + 1; //SecLev, FrameCnt, KeyIndex
			break;
		case 2:
			auxilary_len = frameCntLength + 1 + 3; //SecLev, FrameCnt, KeyIdMode (2 bytes) and KeyIndex
			break;
		case 3:
			auxilary_len = frameCntLength + 1 + 9; //SecLev, FrameCnt, KeyIdMode (8 bytes) and KeyIndex
			break;
		default:
			break;
	}

	return auxilary_len;
}


/**
\brief Verify if the key used to protect the frame has been used properly.
*/
bool IEEE802154security_incomingKeyUsagePolicyChecking(m_keyDescriptor* keyDescriptor,
                                                       uint8_t          frameType,
                                                       uint8_t          cfi){

	uint8_t i;
	INTERRUPT_DECLARATION();
	DISABLE_INTERRUPTS();
	for(i=0; i<MAXNUMNEIGHBORS; i++){
		if (frameType != IEEE154_TYPE_CMD && frameType == keyDescriptor->KeyUsageList[i].FrameType){
			ENABLE_INTERRUPTS();
			return TRUE;
		}

		if (frameType == IEEE154_TYPE_CMD && frameType == keyDescriptor->KeyUsageList[i].FrameType &&
			cfi == keyDescriptor->KeyUsageList[i].CommandFrameIdentifier){
			ENABLE_INTERRUPTS();
			return TRUE;
		}
	}

	ENABLE_INTERRUPTS();
	return FALSE;
}

/**
\brief Verify if the Security Level of the incoming Frame is acceptable or not.
*/
bool IEEE802154security_incomingSecurityLevelChecking(m_securityLevelDescriptor* seclevdesc,
                                                      uint8_t                    seclevel,
                                                      bool                       exempt){

	if (seclevdesc->AllowedSecurityLevels == 0){
		if(seclevel <= seclevdesc->SecurityMinimum){
			return TRUE;
		}else{
			return FALSE;
		}
	}

	if(seclevel <= seclevdesc->AllowedSecurityLevels){
		return TRUE;
	}

	if(seclevel == 0 && seclevdesc->DeviceOverrideSecurityMinimum ==TRUE ){
		if(exempt == FALSE){
			return FALSE;
		}

		return TRUE;
	}

	return FALSE;
}

/**
\brief Searching for the Security Level Descriptor.
*/
m_securityLevelDescriptor* IEEE802154security_securityLevelDescriptorLookup(uint8_t frameType,
                                                                            uint8_t cfi){

	uint8_t i;
	INTERRUPT_DECLARATION();
	DISABLE_INTERRUPTS();
	for(i=0; i<4; i++){

		if(security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].FrameType != IEEE154_TYPE_CMD
			&& frameType == security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].FrameType){

			ENABLE_INTERRUPTS();
			return &security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i];
		}

		if(security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].FrameType == IEEE154_TYPE_CMD
			&& frameType == security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].FrameType
			&& cfi == security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i].CommandFrameIdentifier){

			ENABLE_INTERRUPTS();
			return &security_vars.MacSecurityLevelTable.SecurityDescriptorEntry[i];
		}
	}

	ENABLE_INTERRUPTS();
	return NULL;
}

/**
\brief Searching for the Device Descriptor.
*/
m_deviceDescriptor* IEEE802154security_deviceDescriptorLookup(open_addr_t* Address,
                                                              open_addr_t* PANId,
                                                              m_keyDescriptor* keyDescriptor){

	uint8_t i;
	INTERRUPT_DECLARATION();
	DISABLE_INTERRUPTS();

	for(i=0; i<MAXNUMNEIGHBORS; i++){
		if((packetfunctions_sameAddress(Address,
				&keyDescriptor->DeviceTable->DeviceDescriptorEntry[i].deviceAddress)== TRUE)
			&&(packetfunctions_sameAddress(PANId,
				&security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.PANId))
				){
			ENABLE_INTERRUPTS();
			return &keyDescriptor->DeviceTable->DeviceDescriptorEntry[i];
		}
	}

	ENABLE_INTERRUPTS();
	return NULL;
}

/**
\brief Searching for the Key Descriptor.
*/
m_keyDescriptor* IEEE802154security_keyDescriptorLookup(uint8_t      KeyIdMode,
                                                        open_addr_t* keySource,
                                                        uint8_t      KeyIndex,
                                                        open_addr_t* DeviceAddress,
                                                        open_addr_t* panID,
                                                        uint8_t      frameType){

	uint8_t i;
	INTERRUPT_DECLARATION();
	DISABLE_INTERRUPTS();

	if(KeyIdMode == 0){
	for(i=0; i<MAXNUMKEYS; i++ ){
		if(security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.Address.type == ADDR_64B){
			if(packetfunctions_sameAddress(DeviceAddress,&security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.Address)
					&& packetfunctions_sameAddress(panID,&security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.PANId)){
				ENABLE_INTERRUPTS();
				return &security_vars.MacKeyTable.KeyDescriptorElement[i];
				}
			}
		}

	}

	if (KeyIdMode == 1){
		for(i=0; i<MAXNUMKEYS; i++ ){
				if(KeyIndex == security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.KeyIndex
							&& packetfunctions_sameAddress(keySource,&security_vars.m_macDefaultKeySource)){
					ENABLE_INTERRUPTS();
					return &security_vars.MacKeyTable.KeyDescriptorElement[i];
							}
						}
			}

	if (KeyIdMode == 2){

			for(i=0; i<MAXNUMKEYS; i++ ){
					if(KeyIndex == security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.KeyIndex){

					if( keySource->addr_16b[0] == security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.KeySource.addr_16b[0] &&
							keySource->addr_16b[1] == security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.KeySource.addr_16b[1]
							&& packetfunctions_sameAddress(panID, &security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.PANId)
							){
						ENABLE_INTERRUPTS();
						return &security_vars.MacKeyTable.KeyDescriptorElement[i];
					}

				}
			}
		}

	if (KeyIdMode == 3){

		for(i=0; i<MAXNUMKEYS; i++ ){
				if(KeyIndex == security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.KeyIndex){

				if( packetfunctions_sameAddress(keySource,&security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.KeySource)
						&& packetfunctions_sameAddress(panID, &security_vars.MacKeyTable.KeyDescriptorElement[i].KeyIdLookupList.PANId)
						){
					ENABLE_INTERRUPTS();
					return &security_vars.MacKeyTable.KeyDescriptorElement[i];
				}

			}
		}
	}

	//no matches
	ENABLE_INTERRUPTS();
	return NULL;

}

/*
 * Store in the array the reference value 
 */
void IEEE802154security_getFrameCounter(macFrameCounter_t reference,
                                        uint8_t*          array) {
   array[0]         = (reference.bytes0and1     & 0xff);
   array[1]         = (reference.bytes0and1/256 & 0xff);
   array[2]         = (reference.bytes2and3     & 0xff);
   array[3]         = (reference.bytes2and3/256 & 0xff);
   array[4]         =  reference.byte4;
}