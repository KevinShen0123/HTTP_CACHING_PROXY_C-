From gcc

RUN apt update && apt-get -y --no-install-recommends install build-essential libboost-all-dev
# RUN apt-get install wget&&apt-get install tar&&wget https://boostorg.jfrog.io/artifactory/main/release/1.80.0/source/boost_1_80_0.tar.gz&&tar xvf boost_1_80_0.tar.gz&&cd  boost_1_80_0&&./bootstrap.sh --prefix=/usr/&&./b2 install
RUN mkdir /var/log/erss
RUN mkdir /HTTPProxy
WORKDIR /HTTPProxy
ADD . /HTTPProxy

CMD ["bash","start.sh"]