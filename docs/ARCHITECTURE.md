# 🏛 System Architecture

The Apple Sorting System is a hybrid edge-cloud application.

## High Level Workflow

```mermaid
graph TD
    A[IR Sensor Detects Apple] --> B[Stop Conveyor Belt]
    B --> C[Trigger Cameras Async]
    C --> D[Save Top & Side Images to /tmp/]
    D --> E[POST Images via libcurl]
    E --> F((Cloud API - ngrok))
    
    subgraph Colab Cloud Server
        F --> G[Flask /api/classify]
        G --> H[Run Inference Top Image]
        G --> I[Run Inference Side Image]
        H --> J{Conservative Aggregation}
        I --> J
        J --> K[Return Final Class JSON]
    end
    
    K --> L[Parse JSON Response]
    L --> M{Is Class Normal?}
    M -- Yes --> N[Restart Belt, Let Apple Pass]
    M -- No --> O[Fire Appropriate Servo]
    O --> N
```

## AI Model Details
- **Model**: Vision Transformer (ViT) - `vit_base_patch16_224`
- **Library**: `timm` (PyTorch Image Models)
- **Optimizer**: AdamW
- **Scheduler**: CosineAnnealingLR
- **Loss Function**: Weighted CrossEntropyLoss with label smoothing to handle dataset imbalances.

## Aggregation Logic
Because the system captures two views of the same apple, it must reconcile potentially different predictions:
- **Conservative Aggregation**: If *either* camera detects a disease (Blotch, Rot, or Scab), the apple is marked as diseased. Both cameras must predict "Normal" for the apple to pass.
- **Confidence Threshold**: If the model confidence is below 0.50, the object is rejected as a non-apple or unknown anomaly.
