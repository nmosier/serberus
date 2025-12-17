FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=1

RUN apt-get update && apt-get install -y \
    libgoogle-perftools-dev google-perftools libunwind-dev cmake \
    ninja-build build-essential python3 python-is-python3 \
    git pkg-config python3-venv
RUN python3 -m venv /venv
ENV PATH="/venv/bin:$PATH"
RUN pip install pandas seaborn

ENTRYPOINT ["/bin/bash"]
WORKDIR "/serberus"

RUN apt-get update && apt-get install -y sudo
