/**
 */
#include "common.h"
#include <stdlib.h>

// === CODE ===
/**
 * \brief Dispense an item for a user
 * 
 * The core of the dispense system, I kinda like it :)
 */
int DispenseItem(int User, tItem *Item)
{
	 int	ret;
	tHandler	*handler;
	char	*username;
	char	*reason;
	
	handler = Item->Handler;
	
	// Check if the dispense is possible
	if( handler->CanDispense ) {
		ret = handler->CanDispense( User, Item->ID );
		if(ret)	return 1;	// 1: Unable to dispense
	}

	// Subtract the balance
	reason = mkstr("Dispense - %s:%i %s", handler->Name, Item->ID, Item->Name);
	if( !reason )	reason = Item->Name;	// TODO: Should I instead return an error?
	ret = Transfer( User, GetUserID(COKEBANK_SALES_ACCT), Item->Price, reason);
	free(reason);
	if(ret)	return 2;	// 2: No balance
	
	// Get username for debugging
	username = GetUserName(User);
	
	// Actually do the dispense
	if( handler->DoDispense ) {
		ret = handler->DoDispense( User, Item->ID );
		if(ret) {
			Log_Error("Dispense failed after deducting cost (%s dispensing %s - %ic)",
				username, Item->Name, Item->Price);
			Transfer( GetUserID(COKEBANK_SALES_ACCT), User, Item->Price, "rollback" );
			free( username );
			return -1;	// 1: Unkown Error again
		}
	}
	
	// And log that it happened
	Log_Info("dispense %s (%s:%i) by %s [cost %i, balance %i cents]",
		Item->Name, handler->Name, Item->ID,
		username, Item->Price, GetBalance(User)
		);
	
	free( username );
	return 0;	// 0: EOK
}

/**
 * \brief Give money from one user to another
 */
int DispenseGive(int SrcUser, int DestUser, int Ammount, const char *ReasonGiven)
{
	 int	ret;
	if( Ammount < 0 )	return 1;	// Um... negative give? Not on my watch!
	
	ret = Transfer( SrcUser, DestUser, Ammount, ReasonGiven );
	if(ret)	return 2;	// No Balance
	
	Log_Info("give %i to %s from %s (%s)",
		Ammount, GetUserName(DestUser), GetUserName(SrcUser), ReasonGiven
		);
	
	return 0;
}

int DispenseAdd(int User, int ByUser, int Ammount, const char *ReasonGiven)
{
	 int	ret;
	
	ret = Transfer( GetUserID(COKEBANK_DEBT_ACCT), User, Ammount, ReasonGiven );
	
	if(ret)	return 2;
	
	Log_Info("add %i to %s by %s (%s)",
		Ammount, GetUserName(User), GetUserName(ByUser), ReasonGiven
		);
	
	return 0;
}
