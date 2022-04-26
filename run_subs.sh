clients=$1
topics=$2

for ((i=0;i<clients;i++)); do
	topic_id=$((i%topics))
	mosquitto_sub -V 311 -h localhost -t "<$topic_id> topico_relativamente_grande <$topic_id>" > /dev/null & 
done

echo "use 'killall mosquitto_sub' após o uso"
