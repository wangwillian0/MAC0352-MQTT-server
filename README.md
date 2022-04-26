Para compilar o binário principal do servidor e gerar o binário com o nome "broker":
	
	$ make

Para usar o ambiente docker (a porta 1883 será utilizada assim como o servidor mosquitto):
	
	$ docker-compose build
	
	$ docker-compose up -d

Para usar o script de testes utilizado nos benchmarks:

	$ run_tests.sh <# de clientes> <# de topicos> <tempo de monitoramento (s)> <# de testes>



Sobre os arquivos presentes:

slides.pdf - Apresentação em slides do que foi realizado no EP.

broker.c - Código fonte do servidor broker MQTT descrito pelo enunciado. 

Makefile - Ajuda na compilação (mesmo não existindo nenhuma dependência inesperada).

run_tests.sh - Script em bash utilizado para fazer os testes/benchmarks

Dockerfile - Arquivo que define o ambiente dos testes/benchmark 

docker-compose.yml - Complementa o Dockerfile

