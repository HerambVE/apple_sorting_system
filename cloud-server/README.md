# ☁️ Cloud AI Server

This directory contains the Google Colab notebook used to train the Vision Transformer and serve the Flask API.

## Files
- `optimus_prime.ipynb`: The main notebook. Handles data downloading, model training, and API hosting.
- `requirements.txt`: Python dependencies required for the project.

## API Endpoint Reference

### `POST /api/classify`
Accepts multipart form-data with two images and returns a classification result.

**Request:**
- `image1`: Top view image file
- `image2`: Side view image file

**Response (JSON):**
```json
{
  "class": "Rot",
  "confidence": 0.94,
  "views": {
    "top": "Rot",
    "side": "Normal"
  }
}
```

The server aggregates the views conservatively: if any view shows a disease, the overall class is marked as that disease.
