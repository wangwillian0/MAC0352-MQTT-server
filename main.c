#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

/** Para usar o mkfifo() **/
#include <sys/stat.h>
/** Para usar readdir() e listar os arquivos FIFO nos diretórios **/
#include <dirent.h>
/** Para usar o open e conseguir abrir o pipe **/
#include <fcntl.h>
/** Para usar prctl() e matar o processo filho quando o processo pai é morto **/
#include <sys/prctl.h>
/** Para usar signal() e ignorar o código de retorno do filho (sem um processo zumbi) **/
#include <signal.h>

#define LISTENQ 1
#define MAXBUFFER 4096
#define MAXPATH 65536+101

/* Função que ajuda a fazer o parse dos bytes de "msg len" para int */
int get_msg_len(FILE* connfile) {
    unsigned char cur_byte = 0;
    int shifts = 0, total = 0;
    do {
        cur_byte = getc(connfile); 
        total += (cur_byte & 0x7F) << shifts;
        shifts += 7;
    } while (cur_byte & 0x80);
    return total;
}

/* Função que ajuda a converter int para os bytes de "msg len" no formato exigido */
void put_msg_len(int pipefd, int total) {
    do {
        unsigned char cur_byte = total & 0x7F;
        total >>= 7;
        if (total) cur_byte |= 0x80;
        write(pipefd, &cur_byte, 1);
    } while (total);
}


int main(int argc, char** argv) {

    if (argc != 2) {
        fprintf(stderr,"Uso: %s <Porta>\n",argv[0]);
        fprintf(stderr,"Vai rodar um servidor broker MQTT na porta <Porta> TCP\n");
        exit(1);
    }


    int listenfd, connfd;
    struct sockaddr_in servaddr;

    /* Criação de um socket */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket :(\n");
        exit(2);
    }
    
    /* Inicialização do endereço e associação dele ao socket */
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(atoi(argv[1]));
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        perror("bind :(\n");
        exit(3);
    }

    /* Socket passivo */
    if (listen(listenfd, LISTENQ) == -1) {
        perror("listen :(\n");
        exit(4);
    }


    /* O diretório temporário será utilizado como um banco de dados, na qual cada 
    * subdiretório representará um tópico distinto e conterá todos os arquivos FIFO de 
    * processos que cuidam de clientes inscritos naquele tópico. Mais detalhes na parte 
    * do código que processa os bytes do tópico. */
    char path[MAXPATH] = "/tmp/ep1-willian_wang.XXXXXX";
    int path_len = strlen(path);
    if (mkdtemp(path) == NULL) {
        perror("mkdtemp :(\n");
        exit(5);
    }


    while (1) {
        if ((connfd = accept(listenfd, (struct sockaddr *) NULL, NULL)) == -1 ) {
            perror("accept :(\n");
            exit(6);
        }

        /* Esse fork é para "liberar" o socket do processo principal para novos clientes.
         * O tratamento do sinal SIGCHLD faz com que o status de saída dos filhos possam ser 
         * ignorados, evitando processos zumbis ou a necessidade de um tratamento mais
         * detalhado usando wait. */
        signal(SIGCHLD, SIG_IGN);
        int childpid = fork();


        /* processo pai apenas fecha o socket que está sendo tratado pelo filho e continua
         * com o loop para tratar conexões de novos clientes. */
        if (childpid != 0) {
            close(connfd);
            continue;
        }


        /* O socket listenfd é fechado no processo filho, pois não é mais necessário.
         * connfile é utilizado para possibilitar o uso de getc e facilitar o processamento 
         * de cada byte do pacote. */
        close(listenfd);
        FILE* connfile = fdopen(connfd, "r"); 

        /* Variáveis auxiliares para as "header flags", "msg len" e remain_bytes ajuda a
         * determinar quando o pacote chega ao fim. */
        unsigned char header = 0;
        int msg_len = 0, remain_bytes = 0;

        /* Lê o header do pacote, que deveria ser de CONNECT.
         * O corpo inteiro deste pacote é ignorado. */
        header = getc(connfile);
        msg_len = remain_bytes = get_msg_len(connfile);
        while (remain_bytes--) getc(connfile);

        /* Manda de volta um CONNACK padrão */
        write(connfd, (char []) {0x20, 0x02, 0x00, 0x00}, 4);

        /* Lê o header do e o tamanho do pacote, que deveria ser de PUBLISH ou SUBSCRIBE */
        header = getc(connfile);
        msg_len = remain_bytes = get_msg_len(connfile);
        
        /* O tipo SUBSCRIBE lê dois bytes de identifier que serão ignorados */
        if (header >> 4 == 8) {
            getc(connfile);
            getc(connfile);
            remain_bytes -= 2;
        }

        /* Lê o tamanho do tópico, o tratamento manual tira a necessidade do ntohl aqui */
        int topic_len = getc(connfile) << 8;
        topic_len += getc(connfile);
        remain_bytes -= 2;        

        /* Lê o tópico e atualiza a path com o caminho correspondente ao diretório que irá
         * administrar os arquivos pipe temporários dos clientes inscritos
         *
         * Exemplo de diretório para o tópico "temperatura":
         * /ep1-willian_wang.hIIGh4/temperatura
         */
        char topic[topic_len + 1];
        for (int i = 0; i < topic_len; i++) topic[i] = getc(connfile);
        topic[topic_len] = '\0';
        snprintf(path+path_len, topic_len+2, "/%s", topic);
        remain_bytes -= topic_len;
        path_len += 1+topic_len;


        /* Para tratar o PUBLISH, a mensagem a ser enviada é lida, todos os arquvivos FIFO
         * no diretório temporário correspondente ao tópico são listados, e escrever 
         * o pacote de PUBLISH completo será escrito em cada um deles, byte por byte.*/
        if (header >> 4 == 3) {
            /* Lê a mensagem inteira e guarda na memória */ 
            int content_len = remain_bytes;
            char content[content_len + 1];
            for (int i = 0; i < content_len; i++) content[i] = getc(connfile);
            content[content_len] = '\0';
            remain_bytes = 0;

            /* Itera por todos os potenciais arquivos pipe de processos cuidando de clientes
             * inscritos naquele tópico e escreve todos os bytes que esses outros processos
             * deverão enviar aos clientes que eles estão servindo. */
            DIR* dir = opendir(path);
            struct dirent *file;
            if (dir) {
                while ((file = readdir(dir)) != NULL) {
                    /* Ignora os arquviso que não são FIFO */
                    if (file->d_type != DT_FIFO) continue;

                    snprintf(path+path_len, 257, "/%s", file->d_name);
                    int pipefd = open(path, O_WRONLY);
                    if (pipefd == -1) continue;

                    /* Escreve no arquivo FIFO o pacote PUBLISH completo */
                    write(pipefd, &header, 1);
                    put_msg_len(pipefd, msg_len);
                    char topic_len_bytes[] = {topic_len & 0xF0, topic_len & 0x0F};
                    write(pipefd, topic_len_bytes, 2);
                    write(pipefd, topic, topic_len);
                    write(pipefd, content, content_len);
                    
                    close(pipefd);
                }
                closedir(dir);
            }
        }

        /* Para tratar o SUBSCRIBE, o arquivo FIFO correspondente deve ser lido e repassado
         * para o socket do jeito que se encontra. Ao mesmo tempo, os pacotes PINGREQ e
         * DISCONNECT devem ser tratados para manter a conexão ativa e controlar o fim da 
         * leitura do FIFO. Para isso o fork() é utilizado, com o processo filho cuidando
         * apenas do repasse dos bytes encontrados no arquivo FIFO.  */
        else if (header >> 4 == 8) {
            /* Lê a flag de QoS e ignora ela */
            getc(connfile);

            /* Cria o diretório do tópico temporário caso não exista e cria o arquivo FIFO
             * dentro deste diretório. O nome do arquivo FIFO é o PID do processo escutando.
             *
             * Exemplo de arquivo FIFO:
             * /ep1-willian_wang.hIIGh4/temperatura/27534
             */
            mkdir(path, 0700);
            snprintf(path+path_len, 16, "/%d.fifo", getpid());
            mkfifo(path, 0600);

            int childpid2 = fork();

            /* Processo pai cuidando apenas dos pacotes do tipo PINGREQ e DISCONNECT.
             * Note que o DISCONNECT acaba matando o processo, forçando o processo filho 
             * cuidando do arquivo FIFO a morrer também. */ 
            if (childpid2 != 0) {
                while (1) {
                    header = getc(connfile);
                    get_msg_len(connfile);

                    if (header >> 4 == 12) write(connfd, (char []) {0xD0, 0x00}, 2);
                    else if (header >> 4 == 14) break;
                }
                /* Remove o arquivo FIFO para efeitos de organização. */
                unlink(path);
            }

            /* processo filho cuidando de novos bytes encontrados no arquivo FIFO */
            else {
                /* Manda de voltar um SUBACK padrão */
                write(connfd, (char []) {0x90, 0x03, 0x00, 0x01, 0x00}, 5);

                /* prctl garantirá que a morte do processo pai matará este processo também */ 
                prctl(PR_SET_PDEATHSIG, SIGTERM);

                int pipefd, n = 0;
                unsigned char buf[MAXBUFFER+1]; 

                /* O while é necessário pois todo novo PUBLISH acaba fechando o pipefd. */
                while (1) {
                    pipefd = open(path, O_RDONLY);
                    while ((n=read(pipefd, buf, MAXBUFFER)) > 0) write(connfd, buf, n);
                    if (n < 0) break;
                }
            }
        }


        fclose(connfile);
        close(connfd);
        exit(0);
    } 
}