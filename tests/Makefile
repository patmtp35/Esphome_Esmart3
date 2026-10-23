CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -g

COMP := ../components/esmart3

test: test_protocol test_hub
	./test_protocol
	./test_hub

# Couche protocole seule (aucune dépendance ESPHome)
test_protocol: test_protocol.cpp $(COMP)/esmart3_protocol.h
	$(CXX) $(CXXFLAGS) -o $@ test_protocol.cpp

# Composant complet contre de fausses en-têtes ESPHome (tests/stubs) : UART et horloge simulées
test_hub: test_hub.cpp $(COMP)/esmart3.cpp $(COMP)/esmart3.h $(COMP)/esmart3_protocol.h
	$(CXX) $(CXXFLAGS) -Istubs -I$(COMP) -o $@ test_hub.cpp $(COMP)/esmart3.cpp

clean:
	rm -f test_protocol test_hub

.PHONY: test clean
