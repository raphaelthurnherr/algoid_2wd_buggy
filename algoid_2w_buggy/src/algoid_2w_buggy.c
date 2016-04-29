#define TASK_NUMBER 0
#define ACTION_ALGOID_ID 1
#define ACTION_COUNT 2

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "algoidCom/messagesManager.h"
#include "algoidCom/linux_json.h"
#include "algoidCom/udpPublish.h"
#include "hwGpio/hw_ctrl.h"
#include "tools.h"
#include "algoid_2wd_buggy.h"
#include "timerManager.h"

int ActionTable[10][3];

int createBuggyTask(int MsgId, int actionCount);
int removeBuggyTask(int actionNumber);

// Traitement du message algoid recu
int processAlgoidMsg(void);

int make2WDaction(void);
int setWheelAction(int actionNumber, int wheelNumber, int veloc, int time);
int endWheelAction(int actionNumber, int wheelNumber);

int getWDvalue(char * wheelName);
char reportBuffer[256];

int main(void) {
	if(InitMessager()) printf ("# Creation tache messagerie : ERREUR\n");
	else printf ("# Demarrage tache Messager: OK\n");

	if(InitTimerManager()) printf ("# Creation tache timer : ERREUR\n");
		else printf ("# Demarrage tache timer: OK\n");

	// Initialisation UDP pour broadcast IP Adresse
	initUDP();

	// Initialisation GPIO pour commande moteur
	if(GpioSetup()){
		printf("# Initialisation hardware: OK\n");
		sendMqttReport(0,"# Initialisation hardware: OK\n");
	}
	else{
		printf("# Initialisation hardware: ERREUR\n");
		sendMqttReport(0,"# Initialisation hardware: ERREUR\n");
	}

	while(1){
		int kbInput;
//		kbInput = mygetch();

		if(kbInput=='q')
			break;

// COMMANDE ALGOID RECUE
		if(pullMsgStack(0)){
//			printf("[main] messageID:  %d  param: %d   cmd: %d\n\n",AlgoidCommand.msgID,AlgoidCommand.msgParam,AlgoidCommand.msgType );
			switch(AlgoidCommand.msgType){
				case COMMAND : processAlgoidMsg(); break;
				//case REQUEST : processMsgRequest(); break;
				default : break;
			}
		}


    	if(0){
    		char udpMessage[50];
    		sprintf(&udpMessage[0], "[ %s ] I'm here",ClientID);
    		sendUDPHeartBit(udpMessage);
		    printf("\n MYMAC %s", getMACaddr());
    	}

    	usleep(100000);
	}

	GpioClose();
	int endState=CloseMessager();
	if(!endState)
		  printf( "# ARRET tache Messager - status: %d\n", endState);
	else printf( "# ARRET tache Messager erreur - status: %d\n", endState);

	return EXIT_SUCCESS;
}

// -------------------------------------------------------------------
// PROCESSCOMMAND
// -------------------------------------------------------------------
int processAlgoidMsg(void){
	switch(AlgoidCommand.msgParam){
		case LL_2WD : make2WDaction(); break;
		default : break;
	}
	return 0;
}

// -------------------------------------------------------------------
// make2WDaction
// -------------------------------------------------------------------
int make2WDaction(void){
	int ptrData;
	int myTaskId;
	// Cr�ation d'une t�che pour les toutes les actions � effectuer
	// Recois un num�ro de tache en retour

	myTaskId=createBuggyTask(AlgoidCommand.msgID, 2);			// 2 actions pour mouvement 2WD

	if(myTaskId>0){
		printf("Creation de tache: #%d\n", myTaskId);
		ptrData=getWDvalue("left");
		if(ptrData >=0){
			setWheelAction(myTaskId, WHEEL_LEFT, AlgoidCommand.msgValArray[ptrData].velocity, AlgoidCommand.msgValArray[ptrData].time);
		}

		ptrData=getWDvalue("right");
		if(ptrData >=0){
			setWheelAction(myTaskId, WHEEL_RIGHT, AlgoidCommand.msgValArray[ptrData].velocity, AlgoidCommand.msgValArray[ptrData].time);
		}
		if((AlgoidCommand.msgValArray[ptrData].velocity < -100) ||(AlgoidCommand.msgValArray[ptrData].velocity > 100))
			sendResponse(AlgoidCommand.msgID, "warning", "2wd", "test", 999);
		return 0;
	}
	else return 1;
}


// -------------------------------------------------------------------
// SETWHEELACTION
// Effectue l'action sur une roue sp�cifi�e
// - D�marrage du timer avec definition de fonction call-back, et no d'action
// - D�marrage du mouvement des roues
// -------------------------------------------------------------------
int setWheelAction(int actionNumber, int wheelNumber, int veloc, int time){
	int myDirection;

	if(veloc > 0) myDirection=BUGGY_FORWARD;
	if(veloc == 0) myDirection=BUGGY_STOP;

	if(veloc < 0){
		myDirection=BUGGY_BACK;
		veloc *=-1;
	}

	// Start timer and set callbackback function with arg for stop
	if(setTimerWheel(time, &endWheelAction, actionNumber, wheelNumber)){
		if(setMotor(wheelNumber,myDirection, veloc)){
			sprintf(reportBuffer, "Start wheel %d with velocity %d for time %d\n",wheelNumber, veloc, time);
			printf(reportBuffer);
			sendMqttReport(actionNumber, reportBuffer);

		}
		else{
			sprintf(reportBuffer, "Error, impossible to start wheel %d\n",wheelNumber, veloc, time);
			printf(reportBuffer);
			sendMqttReport(actionNumber, reportBuffer);
		}

	}
	else printf("Error, Impossible to set timer \n");
	return 0;
}

// -------------------------------------------------------------------
// END2WDACTION
// Fin de l'action sur une roue, (Appeler apres timeout)
// -------------------------------------------------------------------
int endWheelAction(int actionNumber, int wheelNumber){
	int result;
	//printf("Action number: %d - End of timer for wheel No: %d\n",actionNumber , wheelNumber);

	// Stop le moteur
	setMotor(wheelNumber,BUGGY_STOP, 0);

	// Retire l'action de la table et v�rification si toute les actions sont effectu�es
	// Pour la t�che en cours

	result=removeBuggyTask(actionNumber);
	if(result){
		sendResponse(result, "event","2wd", 0, 0);
		sprintf(reportBuffer, "FIN DES ACTIONS \"WHEEL\" pour la tache #%d\n", actionNumber);
		printf(reportBuffer);
		sendMqttReport(actionNumber, reportBuffer);
	}

	return 0;
}

// -------------------------------------------------------------------
// GETWDVALUE
// -------------------------------------------------------------------
int getWDvalue(char* wheelName){
	int i;
	int searchPtr = -1;
	char searchText[50];
	char * mySearch;

	// Recherche dans les donn�e recues la valeur correspondante au param�tre "wheelName"

	for(i=0;i<AlgoidCommand.msgValueCnt;i++){
		memset(searchText, 0, 50);
		mySearch=AlgoidCommand.msgValArray[i].wheel;
		strncpy(searchText,mySearch, strlen(AlgoidCommand.msgValArray[i].wheel));

		if(!strcmp(searchText, wheelName))
			searchPtr=i;
	}
	return searchPtr;
}



// -------------------------------------------------------------------
// CREATBUGGYTASK Creation d'une tache avec le nombre
// d'actions � effectuer
// - Retourne le num�ro d'action attribu�
// - Retourne 0 si table des taches pleine (Impossible de cr�er)
// - Retourne -1 si Message ID existe d�j�
// -------------------------------------------------------------------

int createBuggyTask(int MsgId, int actionCount){
	int i;
	int actionID;

	// d�fini un num�ro de tache al�atoire pour l'action � executer si pas de message id saisi
	if(MsgId == 0){
		actionID = rand() & 0xFFFFFF;
		MsgId = actionID;
	}
	else actionID = MsgId;

	// Recherche un emplacement libre pour inserer les donn�es
	for(i=0;i<10;i++){
		if(ActionTable[i][TASK_NUMBER]==0){
			ActionTable[i][TASK_NUMBER]=actionID;
			ActionTable[i][ACTION_ALGOID_ID]= MsgId;
			ActionTable[i][ACTION_COUNT]=actionCount;
			return(actionID);
		}else{
			if(ActionTable[i][TASK_NUMBER]==actionID)
			{
				sprintf(reportBuffer, "ERREUR: Tache en cours de traitement: %d\n", actionID);
				sendMqttReport(actionID, reportBuffer);
				return -1;
				}
		}
	}
	sprintf(reportBuffer, "ERREUR: Table de t�ches pleine\n", actionID);
	sendMqttReport(actionID, reportBuffer);
	return(0);
}

// -------------------------------------------------------------------
// removeBuggyTask
// Mise � jour, soustrait l'action d'une tache
// - Retourne le MESSAGE ID correspondant � la tache si plus d'action � effectuer
// - Retourne 0 si actions restante
// - Retourne -1 si tache inexistante
// -------------------------------------------------------------------

int removeBuggyTask(int actionNumber){
	int i, algoidMsgId;

	// Recherche la tache correspondante dans la t�ble des action
	for(i=0;i<10;i++){
		if(ActionTable[i][TASK_NUMBER]==actionNumber){
			ActionTable[i][ACTION_COUNT]--;
			//printf("UPDATE ACTION %d  reste: %d\n", actionNumber, ActionTable[i][ACTION_COUNT]);
			if((ActionTable[i][ACTION_COUNT]) <=0){
				algoidMsgId=ActionTable[i][ACTION_ALGOID_ID];
				ActionTable[i][TASK_NUMBER]=0;				// Reset/Lib�re l'occupation de la t�che
				ActionTable[i][ACTION_ALGOID_ID]= 0;
				ActionTable[i][ACTION_COUNT]=0;
				return(algoidMsgId);
			} else return 0;								// Action non termin�es
		}
	}
	return(-1);												// T�che inexistante
}
