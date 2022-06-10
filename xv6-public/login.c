#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{   
    static char id[16];
    static char pw[16];

    char *args[] = {"sh", id, 0};

    while(1){
        printf(2, "User ID : ");
        memset(id, '\0', sizeof(id));
        gets(id, sizeof(id));
        id[strlen(id) - 1] = '\0';

        printf(2, "Password : ");
        memset(pw, '\0', sizeof(pw));
        gets(pw, sizeof(pw));
        pw[strlen(pw) - 1] = '\0';

        if(id[0] == '\0' || id[0] == '\n' || pw[0] == '\0' || pw[0] == '\n'){
            printf(2, "invalid ID or PW\n");
            continue;
        }

        if(accountcheck(id, pw) == 0)
        {
            if(fork2(id) == 0){
                exec("sh", args);
                printf(2, "login: exec sh failed\n");
            }
            wait();
        }
        else
            printf(2, "Login Failed\n");
    }
}