int main ()
{
    setnice(getpid(),5);
    int pid1 = fork();
    int a = 1;
    int range = 2000;

    if(pid1>0){
        int pid2 = fork();
        if(pid2>0){
            int pid3 = fork();

            if(pid3>0){
                wait();
                for(int i = 0; i< range; i++){
                    for(int j = 0; j< range; j++){
                        a = 9.9+9.9*a;
                    }
                }
                printf(1,"%d - ans: %d\n",getpid(),a);
            }
            else if (pid3==0){
                setnice(getpid(),15);
                for(int i = 0; i< range; i++){
                    for(int j = 0; j< range; j++){
                        a = 9.9+9.9*a;
                    }
                }
                printf(1,"%d - ans: %d\n",getpid(),a);
            }
        }
        else if(pid2 == 0){
            setnice(getpid(),10);
            for(int i = 0; i< range; i++){
                for(int j = 0; j< range; j++){
                a = 9.9+9.9*a;
                }
            }
            printf(1,"%d - ans: %d\n",getpid(),a);
        }
    }
    else if(pid1 == 0){
        setnice(getpid(),5);
        for(int i = 0; i< range; i++){
            for(int j = 0; j< range; j++){
            a = 9.9+9.9*a;
            }
        }
        printf(1,"%d - ans: %d\n",getpid(),a);
    }
    
    exit();
}