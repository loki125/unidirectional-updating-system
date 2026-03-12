# Use an official lightweight Python image
FROM python:3.12-slim

# Prevent Python from writing .pyc files to disk and keep stdout/stderr unbuffered
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

# Set the working directory inside the container
WORKDIR /project

# Copy the requirements file into the container
COPY requirements.txt .

# Install the required Python packages
RUN pip install --no-cache-dir -r requirements.txt

# Copy the backend code and frontend static files into the container
COPY ./app ./app
COPY ./static ./static

# Expose port 8000 for the FastAPI server
EXPOSE 8000

# Command to run the Uvicorn server
# Because main.py is inside the 'app' folder, we use 'app.main:app'
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]