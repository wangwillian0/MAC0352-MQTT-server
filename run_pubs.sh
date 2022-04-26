clients=$1
topics=$2

for ((i=0;i<clients;i++)); do
	topic_id=$((i%topics))
	mosquitto_pub -V 311 -h localhost -t "<$topic_id> topico_relativamente_grande <$topic_id>" -m "<$topic_id> ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890!@#$%^&*() mensagem um pouco mais comprido para o topico com topic_id = <$topic_id>." & 
done

echo "use 'killall mosquitto_pub' caso necessário"

