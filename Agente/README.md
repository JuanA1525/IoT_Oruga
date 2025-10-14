# IoT Agent

Componente del agente IoT que actúa como puente entre los dispositivos y la arquitectura de nube.

## Funcionalidades

### Función 1 (Implementada)
- Recibe órdenes de movimiento simuladas
- Expone API REST para consultar instrucciones
- Instrucciones soportadas: `adelante`, `atras`, `izquierda`, `derecha`, `stop`

### Función 2 (Pendiente)
- Recibir datos encriptados de sensores
- Desencriptar datos
- Enviar a arquitectura de nube

## API Endpoints

- `GET /api/instruction` - Obtiene la instrucción actual
- `POST /api/instruction` - Establece una nueva instrucción (simulación)

## Variables de Entorno

Ver `.env.example` en la raíz del proyecto.