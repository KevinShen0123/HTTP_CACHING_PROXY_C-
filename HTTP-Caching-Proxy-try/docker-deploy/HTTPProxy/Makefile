TARGETS=proxy

all: $(TARGETS)
clean:
	rm -f $(TARGETS)

proxy: proxy.cpp Cache.hpp parser.hpp
	g++ -o $@ $< -Werror -l pthread