/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 *
 * cokebank.c - Coke-Bank management
 *
 * This file is licenced under the 3-clause BSD Licence. See the file COPYING
 * for full details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <string.h>
#include "common.h"

#define HACK_TPG_NOAUTH	1

// === IMPORTS ===
extern int	Bank_GetMinAllowedBalance(int ID);
extern int	Bank_GetUserBalance(int ID);
extern int	Bank_AlterUserBalance(int ID, int Delta);
extern int	Bank_GetUserByUnixID(int UnixID);
extern int	Bank_GetUserUnixID(int ID);
extern int	Bank_AddUser(int UnixID);
extern FILE	*gBank_File;
extern tUser	*gaBank_Users;
extern int	giBank_NumUsers;

// === PROTOTYPES ===
void	Init_Cokebank(const char *Argument);
 int	Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason);
 int	GetBalance(int User);
char	*GetUserName(int User);
 int	GetUserID(const char *Username); 
 int	GetUserAuth(const char *Username, const char *Password);

// === CODE ===
/**
 * \brief Load the cokebank database
 */
void Init_Cokebank(const char *Argument)
{
	gBank_File = fopen(Argument, "rb+");
	if( !gBank_File ) {
		gBank_File = fopen(Argument, "wb+");
	}
	if( !gBank_File ) {
		perror("Opening coke bank");
	}

	fseek(gBank_File, 0, SEEK_END);
	giBank_NumUsers = ftell(gBank_File) / sizeof(gaBank_Users[0]);
	fseek(gBank_File, 0, SEEK_SET);
	gaBank_Users = malloc( giBank_NumUsers * sizeof(gaBank_Users[0]) );
	fread(gaBank_Users, sizeof(gaBank_Users[0]), giBank_NumUsers, gBank_File);
}

/**
 * \brief Transfers money from one user to another
 * \param SourceUser	Source user
 * \param DestUser	Destination user
 * \param Ammount	Ammount of cents to move from \a SourceUser to \a DestUser
 * \param Reason	Reason for the transfer (essentially a comment)
 * \return Boolean failure
 */
int Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason)
{
	if( Bank_GetUserBalance(SourceUser) - Ammount < Bank_GetMinAllowedBalance(SourceUser) )
		return 1;
	if( Bank_GetUserBalance(DestUser) + Ammount < Bank_GetMinAllowedBalance(DestUser) )
		return 1;
	Bank_AlterUserBalance(DestUser, Ammount);
	Bank_AlterUserBalance(SourceUser, -Ammount);
	return 0;
}

/**
 * \brief Get the balance of the passed user
 */
int GetBalance(int User)
{
	return 0;
}

/**
 * \brief Return the name the passed user
 */
char *GetUserName(int User)
{
	struct passwd	*pwd;
	 int	unixid = Bank_GetUserUnixID(User);
	
	if( unixid == -1 )
		return strdup(">sales");

	if( unixid == -2 )
		return strdup(">liability");

	pwd = getpwuid(unixid);
	if( !pwd )	return NULL;

	return strdup(pwd->pw_name);
}

/**
 * \brief Get the User ID of the named user
 */
int GetUserID(const char *Username)
{
	 int	ret, uid;

	if( strcmp(Username, ">sales") == 0 ) {	// Pseudo account that sales are made into
		uid = -1;
	}
	else if( strcmp(Username, ">liability") == 0 ) {	// Pseudo acount that money is added from
		uid = -2;
	}
	else {
		struct passwd	*pwd;
		// Get user ID
		pwd = getpwnam(Username);
		if( !pwd )	return -1;
		uid = pwd->pw_uid;
	}

	// Get internal ID (or create new user)
	ret = Bank_GetUserByUnixID(uid);
	if( ret == -1 ) {
		ret = Bank_AddUser(uid);
	}

	return ret;
}

/**
 * \brief Authenticate a user
 * \return User ID, or -1 if authentication failed
 */
int GetUserAuth(const char *Username, const char *Password)
{
	#if HACK_TPG_NOAUTH
	if( strcmp(Username, "tpg") == 0 )
		return GetUserID("tpg");
	#endif
	return -1;
}
