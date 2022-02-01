FROM amd64/ubuntu:18.04 AS base

EXPOSE 8766/tcp
EXPOSE 8767/tcp

ENV DEBIAN_FRONTEND=noninteractive

#Add ppa:bitcoin/bitcoin repository so we can install libdb4.8 libdb4.8++
RUN apt-get update && \
	apt-get install -y software-properties-common && \
	add-apt-repository ppa:bitcoin/bitcoin

#Install runtime dependencies
RUN apt-get update && \
	apt-get install -y --no-install-recommends \
	bash net-tools libminiupnpc10 \
	libevent-2.1 libevent-pthreads-2.1 \
	libdb4.8 libdb4.8++ \
	libboost-system1.65 libboost-filesystem1.65 libboost-chrono1.65 \
	libboost-program-options1.65 libboost-thread1.65 \
	libzmq5 && \
	apt-get clean

FROM base AS build

#Install build dependencies
RUN apt-get update && \
	apt-get install -y --no-install-recommends \
	bash net-tools build-essential libtool autotools-dev automake \
	pkg-config libssl-dev libevent-dev bsdmainutils python3 \
	libboost-system1.65-dev libboost-filesystem1.65-dev libboost-chrono1.65-dev \
	libboost-program-options1.65-dev libboost-test1.65-dev libboost-thread1.65-dev \
	libzmq3-dev libminiupnpc-dev libdb4.8-dev libdb4.8++-dev && \
	apt-get clean

#Build Yonacoin from source
COPY . /home/yona/build/Yonacoin/
WORKDIR /home/yona/build/Yonacoin
RUN ./autogen.sh && ./configure --disable-tests --with-gui=no && make

FROM base AS final

#Add our service account user
RUN useradd -ms /bin/bash yona && \
	mkdir /var/lib/yona && \
	chown yona:yona /var/lib/yona && \
	ln -s /var/lib/yona /home/yona/.yona && \
	chown -h yona:yona /home/yona/.yona

VOLUME /var/lib/yona

#Copy the compiled binaries from the build
COPY --from=build /home/yona/build/Yonacoin/src/yonad /usr/local/bin/yonad
COPY --from=build /home/yona/build/Yonacoin/src/yona-cli /usr/local/bin/yona-cli

WORKDIR /home/yona
USER yona

CMD /usr/local/bin/yonad -datadir=/var/lib/yona -printtoconsole -onlynet=ipv4
