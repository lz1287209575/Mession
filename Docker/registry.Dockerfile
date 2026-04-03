FROM python:3.11-slim

ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

WORKDIR /app

COPY . /app

EXPOSE 19080

CMD ["python3", "Scripts/server_registry.py"]
