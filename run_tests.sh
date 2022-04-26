if [[ $# -ne 4 ]] ; then
	echo "Use ./run_tests.sh <clientes> <topicos> <tempo de monitoramento> <quantas vezes testar>"
	exit 0
fi

clients=$1
topics=$2
duration=$3
samples=$4

function monitor_stats {
	calls=$((duration/2))
	for ((i=0;i<calls;i++)); do
		docker stats --no-stream --format "{{.CPUPerc}},{{.NetIO}}" >> $1
	done
}

function run_subs {
	for ((i=0;i<clients;i++)); do
		topic_id=$((i%topics))
		mosquitto_sub -V 311 -h localhost -t "<$topic_id> topico_relativamente_grande <$topic_id>" > /dev/null & 
	done
}

function run_pubs {
	for ((i=0;i<clients;i++)); do
		topic_id=$((i%topics))
		mosquitto_pub -V 311 -h localhost -t "<$topic_id> topico_relativamente_grande <$topic_id>" -m "<$topic_id> ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890!@#$%^&*() mensagem um pouco mais comprido para o topico com topic_id = <$topic_id>." &  
	done
}


for ((i=0;i<samples;i++)); do
	echo "[teste $i] digite algo para continuar"
	read line
	filename="teste_$i"
	monitor_stats $filename &
	run_subs &
	run_pubs &
	echo "digite algo para continuar e finalizar potenciais clientes que ainda estão rodando)"
	read line
	killall mosquitto_pub
	killall mosquitto_sub
done

	
