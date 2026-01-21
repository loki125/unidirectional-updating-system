FROM python:3.12-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        net-tools \
        curl \
        wget \
        vim \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Install Python deps
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

ENV PYTHONUNBUFFERED=1

COPY app/ /app

EXPOSE 8000

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]