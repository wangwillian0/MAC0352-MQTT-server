FROM ubuntu

RUN apt-get update -y && apt-get install make gcc -y

COPY broker.c .

RUN make broker

CMD ./broker 1883 
