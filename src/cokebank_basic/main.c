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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <openssl/sha.h>
#include "common.h"
#if USE_LDAP
# include <ldap.h>
#endif

/*
 * NOTES:
 * 
 * http://linuxdevcenter.com/pub/a/linux/2003/08/14/libldap.html
 * - Using libldap, the LDAP Client Library 
 * 
 */

#define USE_UNIX_GROUPS	1
#define HACK_TPG_NOAUTH	1
#define HACK_ROOT_NOAUTH	1

// === PROTOTYPES ===
void	Init_Cokebank(const char *Argument);
static int Bank_int_ReadDatabase(void);
static int Bank_int_WriteEntry(int ID);
 int	Bank_Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason);
 int	Bank_CreateUser(const char *Username);
 int	Bank_GetMaxID(void);
 int	Bank_GetUserID(const char *Username);
 int	Bank_GetBalance(int User);
 int	Bank_GetFlags(int User);
 int	Bank_SetFlags(int User, int Mask, int Value);
 int	Bank_int_AlterUserBalance(int ID, int Delta);
 int	Bank_int_GetMinAllowedBalance(int ID);
 int	Bank_int_AddUser(const char *Username);
char	*Bank_GetUserName(int User);
 int	Bank_int_GetUnixID(const char *Username);
 int	Bank_GetUserAuth(const char *Salt, const char *Username, const char *PasswordString);
#if USE_LDAP
char	*ReadLDAPValue(const char *Filter, char *Value);
#endif
void	HexBin(uint8_t *Dest, int BufSize, const char *Src);

// === GLOBALS ===
FILE	*gBank_LogFile;
#if USE_LDAP
char	*gsLDAPPath = "ldapi:///";
LDAP	*gpLDAP;
#endif
tUser	*gaBank_Users;
 int	giBank_NumUsers;
FILE	*gBank_File;

// === CODE ===
/**
 * \brief Load the cokebank database
 */
void Init_Cokebank(const char *Argument)
{
	#if USE_LDAP
	 int	rv;
	#endif
	
	// Open Cokebank
	gBank_File = fopen(Argument, "rb+");
	if( !gBank_File )	gBank_File = fopen(Argument, "wb+");
	if( !gBank_File )	perror("Opening coke bank");
	Bank_int_ReadDatabase();

	// Open log file
	// TODO: Do I need this?
	gBank_LogFile = fopen("cokebank.log", "a");
	if( !gBank_LogFile )	gBank_LogFile = stdout;
	
	
	#if USE_LDAP
	// Connect to LDAP
	rv = ldap_create(&gpLDAP);
	if(rv) {
		fprintf(stderr, "ldap_create: %s\n", ldap_err2string(rv));
		exit(1);
	}
	rv = ldap_initialize(&gpLDAP, gsLDAPPath);
	if(rv) {
		fprintf(stderr, "ldap_initialize: %s\n", ldap_err2string(rv));
		exit(1);
	}
	{ int ver = LDAP_VERSION3; ldap_set_option(gpLDAP, LDAP_OPT_PROTOCOL_VERSION, &ver); }
	# if 0
	rv = ldap_start_tls_s(gpLDAP, NULL, NULL);
	if(rv) {
		fprintf(stderr, "ldap_start_tls_s: %s\n", ldap_err2string(rv));
		exit(1);
	}
	# endif
	{
		struct berval	cred;
		struct berval	*servcred;
		cred.bv_val = "secret";
		cred.bv_len = 6;
		rv = ldap_sasl_bind_s(gpLDAP, "cn=admin,dc=ucc,dc=gu,dc=uwa,dc=edu,dc=au",
			"", &cred, NULL, NULL, &servcred);
		if(rv) {
			fprintf(stderr, "ldap_start_tls_s: %s\n", ldap_err2string(rv));
			exit(1);
		}
	}
	#endif
}

#if 1
static int Bank_int_ReadDatabase(void)
{
	if( gaBank_Users )	return 1;
	
	// Get size
	fseek(gBank_File, 0, SEEK_END);
	giBank_NumUsers = ftell(gBank_File) / sizeof(gaBank_Users[0]);
	fseek(gBank_File, 0, SEEK_SET);
	// Read data
	gaBank_Users = malloc( giBank_NumUsers * sizeof(gaBank_Users[0]) );
	fread(gaBank_Users, sizeof(gaBank_Users[0]), giBank_NumUsers, gBank_File);
	
	return 0;
}
#else
static int Bank_int_ReadDatabase(void)
{
	char	buf[BUFSIZ];
	// Alternate data format
	// > Plain Text
	// <username>,<uid>,<pin>,<lastused>,<balance>,<flags>,<altlogins...>
	fseek(gBank_File, 0, SEEK_SET);
	
	while(1)
	{
		fgets(buf, BUFSIZ-1, gBank_File);
	}
	#endif
}
#endif

static int Bank_int_WriteEntry(int ID)
{
	if( ID < 0 || ID >= giBank_NumUsers ) {
		return -1;
	}
	
	// Commit to file
	fseek(gBank_File, ID*sizeof(gaBank_Users[0]), SEEK_SET);
	fwrite(&gaBank_Users[ID], sizeof(gaBank_Users[0]), 1, gBank_File);
	
	return 0;
}

/**
 * \brief Transfers money from one user to another
 * \param SourceUser	Source user
 * \param DestUser	Destination user
 * \param Ammount	Ammount of cents to move from \a SourceUser to \a DestUser
 * \param Reason	Reason for the transfer (essentially a comment)
 * \return Boolean failure
 */
int Bank_Transfer(int SourceUser, int DestUser, int Ammount, const char *Reason)
{
	 int	srcBal = Bank_GetBalance(SourceUser);
	 int	dstBal = Bank_GetBalance(DestUser);
	
	if( srcBal - Ammount < Bank_int_GetMinAllowedBalance(SourceUser) )
		return 1;
	if( dstBal + Ammount < Bank_int_GetMinAllowedBalance(DestUser) )
		return 1;
	Bank_int_AlterUserBalance(DestUser, Ammount);
	Bank_int_AlterUserBalance(SourceUser, -Ammount);
	fprintf(gBank_LogFile, "Transfer %ic #%i{%i} > #%i{%i} [%i, %i] (%s)\n",
		Ammount, SourceUser, srcBal, DestUser, dstBal,
		srcBal - Ammount, dstBal + Ammount, Reason);
	return 0;
}

int Bank_CreateUser(const char *Username)
{
	 int	ret;
	
	ret = Bank_GetUserID(Username);
	if( ret != -1 )	return -1;
	
	return Bank_int_AddUser(Username);
}

int Bank_GetMaxID(void)
{
	return giBank_NumUsers;
}

/**
 * \brief Get the User ID of the named user
 */
int Bank_GetUserID(const char *Username)
{
	 int	i, uid;
	
	uid = Bank_int_GetUnixID(Username);
	
	// Expensive search :(
	for( i = 0; i < giBank_NumUsers; i ++ )
	{
		if( gaBank_Users[i].UnixID == uid )
			return i;
	}

	return -1;
}

int Bank_GetBalance(int ID)
{
	if( ID < 0 || ID >= giBank_NumUsers )
		return INT_MIN;

	return gaBank_Users[ID].Balance;
}

int Bank_GetFlags(int ID)
{
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;

	// root
	if( gaBank_Users[ID].UnixID == 0 ) {
		gaBank_Users[ID].Flags |= USER_FLAG_WHEEL|USER_FLAG_COKE;
	}

	#if USE_UNIX_GROUPS
	// TODO: Implement checking the PAM groups and status instead, then
	// fall back on the database. (and update if there is a difference)
	if( gaBank_Users[ID].UnixID > 0 )
	{
		struct passwd	*pwd;
		struct group	*grp;
		 int	i;
		
		// Get username
		pwd = getpwuid( gaBank_Users[ID].UnixID );
		
		// Check for additions to the "coke" group
		grp = getgrnam("coke");
		if( grp ) {
			for( i = 0; grp->gr_mem[i]; i ++ )
			{
				if( strcmp(grp->gr_mem[i], pwd->pw_name) == 0 ) {
					gaBank_Users[ID].Flags |= USER_FLAG_COKE;
					break ;
				}
			}
		}
		
		// Check for additions to the "wheel" group
		grp = getgrnam("wheel");
		if( grp ) {
			for( i = 0; grp->gr_mem[i]; i ++ )
			{
				if( strcmp(grp->gr_mem[i], pwd->pw_name) == 0 ) {
					gaBank_Users[ID].Flags |= USER_FLAG_WHEEL;
					break ;
				}
			}
		}
	}
	#endif

	return gaBank_Users[ID].Flags;
}

int Bank_SetFlags(int ID, int Mask, int Value)
{
	// Sanity
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;
	
	// Silently ignore changes to root and meta accounts
	if( gaBank_Users[ID].UnixID <= 0 )	return 0;
	
	gaBank_Users[ID].Flags &= ~Mask;
	gaBank_Users[ID].Flags |= Value;

	Bank_int_WriteEntry(ID);
	
	return 0;
}

int Bank_int_AlterUserBalance(int ID, int Delta)
{
	// Sanity
	if( ID < 0 || ID >= giBank_NumUsers )
		return -1;

	// Update
	gaBank_Users[ID].Balance += Delta;

	Bank_int_WriteEntry(ID);
	
	return 0;
}

int Bank_int_GetMinAllowedBalance(int ID)
{
	 int	flags;
	if( ID < 0 || ID >= giBank_NumUsers )
		return 0;

	flags = Bank_GetFlags(ID);

	// Internal accounts have no limit
	if( (flags & USER_FLAG_INTERNAL) )
		return INT_MIN;

	// Wheel is allowed to go to -$100
	if( (flags & USER_FLAG_WHEEL) )
		return -10000;
	
	// Coke is allowed to go to -$20
	if( (flags & USER_FLAG_COKE) )
		return -2000;

	// For everyone else, no negative
	return 0;
}

/**
 * \brief Create a new user in our database
 */
int Bank_int_AddUser(const char *Username)
{
	void	*tmp;
	 int	uid = Bank_int_GetUnixID(Username);

	// Can has moar space plz?
	tmp = realloc(gaBank_Users, (giBank_NumUsers+1)*sizeof(gaBank_Users[0]));
	if( !tmp )	return -1;
	gaBank_Users = tmp;

	// Crete new user
	gaBank_Users[giBank_NumUsers].UnixID = uid;
	gaBank_Users[giBank_NumUsers].Balance = 0;
	gaBank_Users[giBank_NumUsers].Flags = 0;
	
	if( strcmp(Username, COKEBANK_DEBT_ACCT) == 0 ) {
		gaBank_Users[giBank_NumUsers].Flags = USER_FLAG_INTERNAL;
	}
	else if( strcmp(Username, COKEBANK_SALES_ACCT) == 0 ) {
		gaBank_Users[giBank_NumUsers].Flags = USER_FLAG_INTERNAL;
	}
	else if( strcmp(Username, "root") == 0 ) {
		gaBank_Users[giBank_NumUsers].Flags = USER_FLAG_WHEEL|USER_FLAG_COKE;
	}

	// Increment count
	giBank_NumUsers ++;
	
	Bank_int_WriteEntry(giBank_NumUsers - 1);

	return 0;
}

// ---
// Unix user dependent code
// TODO: Modify to keep its own list of usernames
// ---
/**
 * \brief Return the name the passed user
 */
char *Bank_GetUserName(int ID)
{
	struct passwd	*pwd;
	
	if( ID < 0 || ID >= giBank_NumUsers )
		return NULL;
	
	if( gaBank_Users[ID].UnixID == -1 )
		return strdup(COKEBANK_SALES_ACCT);

	if( gaBank_Users[ID].UnixID == -2 )
		return strdup(COKEBANK_DEBT_ACCT);

	pwd = getpwuid(gaBank_Users[ID].UnixID);
	if( !pwd )	return NULL;

	return strdup(pwd->pw_name);
}

int Bank_int_GetUnixID(const char *Username)
{
	 int	uid;

	if( strcmp(Username, COKEBANK_SALES_ACCT) == 0 ) {	// Pseudo account that sales are made into
		uid = -1;
	}
	else if( strcmp(Username, COKEBANK_DEBT_ACCT) == 0 ) {	// Pseudo acount that money is added from
		uid = -2;
	}
	else {
		struct passwd	*pwd;
		// Get user ID
		pwd = getpwnam(Username);
		if( !pwd )	return -1;
		uid = pwd->pw_uid;
	}
	return uid;
}


/**
 * \brief Authenticate a user
 * \return User ID, or -1 if authentication failed
 */
int Bank_GetUserAuth(const char *Salt, const char *Username, const char *PasswordString)
{
	#if USE_LDAP
	uint8_t	hash[20];
	uint8_t	h[20];
	 int	ofs = strlen(Username) + strlen(Salt);
	char	input[ ofs + 40 + 1];
	char	tmp[4 + strlen(Username) + 1];	// uid=%s
	char	*passhash;
	#endif
	
	#if 1
	// Only here to shut GCC up (until password auth is implemented
	if( Salt == NULL )
		return -1;
	if( PasswordString == NULL )
		return -1;
	#endif
	
	#if HACK_TPG_NOAUTH
	if( strcmp(Username, "tpg") == 0 )
		return Bank_GetUserID("tpg");
	#endif
	#if HACK_ROOT_NOAUTH
	if( strcmp(Username, "root") == 0 ) {
		int ret = Bank_GetUserID("root");
		if( ret == -1 )
			return Bank_CreateUser("root");
		return ret;
	}
	#endif
	
	#if USE_LDAP
	HexBin(hash, 20, PasswordString);
	
	// Build string to hash
	strcpy(input, Username);
	strcpy(input, Salt);
	
	// TODO: Get user's SHA-1 hash
	sprintf(tmp, "uid=%s", Username);
	printf("tmp = '%s'\n", tmp);
	passhash = ReadLDAPValue(tmp, "userPassword");
	if( !passhash ) {
		return -1;
	}
	printf("LDAP hash '%s'\n", passhash);
	
	sprintf(input+ofs, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
		h[ 0], h[ 1], h[ 2], h[ 3], h[ 4], h[ 5], h[ 6], h[ 7], h[ 8], h[ 9],
		h[10], h[11], h[12], h[13], h[14], h[15], h[16], h[17], h[18], h[19]
		);
	// Then create the hash from the provided salt
	// Compare that with the provided hash

	# if 1
	{
		 int	i;
		printf("Password hash ");
		for(i=0;i<20;i++)
			printf("%02x", hash[i]&0xFF);
		printf("\n");
	}
	# endif
	
	#endif
	
	return -1;
}

#if USE_LDAP
char *ReadLDAPValue(const char *Filter, char *Value)
{
	LDAPMessage	*res, *res2;
	struct berval **attrValues;
	char	*attrNames[] = {Value,NULL};
	char	*ret;
	struct timeval	timeout;
	 int	rv;
	
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	
	rv = ldap_search_ext_s(gpLDAP, "", LDAP_SCOPE_BASE, Filter,
		attrNames, 0, NULL, NULL, &timeout, 1, &res
		);
	printf("ReadLDAPValue: rv = %i\n", rv);
	if(rv) {
		fprintf(stderr, "LDAP Error reading '%s' with filter '%s'\n%s\n",
			Value, Filter,
			ldap_err2string(rv)
			);
		return NULL;
	}
	
	res2 = ldap_first_entry(gpLDAP, res);
	attrValues = ldap_get_values_len(gpLDAP, res2, Value);
	
	ret = strndup(attrValues[0]->bv_val, attrValues[0]->bv_len);
	
	ldap_value_free_len(attrValues);
	
	
	return ret;
}
#endif

// TODO: Move to another file
void HexBin(uint8_t *Dest, int BufSize, const char *Src)
{
	 int	i;
	for( i = 0; i < BufSize; i ++ )
	{
		uint8_t	val = 0;
		
		if('0' <= *Src && *Src <= '9')
			val |= (*Src-'0') << 4;
		else if('A' <= *Src && *Src <= 'F')
			val |= (*Src-'A'+10) << 4;
		else if('a' <= *Src && *Src <= 'f')
			val |= (*Src-'a'+10) << 4;
		else
			break;
		Src ++;
		
		if('0' <= *Src && *Src <= '9')
			val |= (*Src-'0');
		else if('A' <= *Src && *Src <= 'F')
			val |= (*Src-'A'+10);
		else if('a' <= *Src && *Src <= 'f')
			val |= (*Src-'a'+10);
		else
			break;
		Src ++;
		
		Dest[i] = val;
	}
	for( ; i < BufSize; i++ )
		Dest[i] = 0;
}

