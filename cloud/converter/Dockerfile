# Converter microservice: runs the pre-built `gdml2obj` (GDML -> OBJ mesh).
# The binary + Geant4 shared libraries are staged by ./stage.sh on the host
# that has Geant4 installed, then copied into this glibc (Ubuntu) image.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
      python3 libxerces-c3.2t64 libcurl3t64-gnutls ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY stage/gdml2obj /usr/local/bin/gdml2obj
RUN chmod +x /usr/local/bin/gdml2obj
COPY stage/lib/ /usr/local/lib/
RUN ldconfig

COPY server.py /app/server.py

EXPOSE 8081
CMD ["python3", "/app/server.py"]
