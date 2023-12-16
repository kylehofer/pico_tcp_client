/*
 * File: PicoTcpClient.cpp
 * Project: pico_tcp_client
 * Created Date: Sunday December 25th 2022
 * Author: Kyle Hofer
 *
 * MIT License
 *
 * Copyright (c) 2022 Kyle Hofer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * HISTORY:
 */

#include "PicoTcpClient.h"

#include <functional>
#include <stdio.h>

#ifdef DEBUGGING
#define DEBUG(format, ...) printf(format, ##__VA_ARGS__);
#else
#define DEBUG(out, ...)
#endif

#define POLL_TIME_S 5

static DataBuffer *initializeDataBuffer(const void *buffer, size_t length)
{
    DataBuffer *dataBuffer;
    dataBuffer = (DataBuffer *)malloc(sizeof(DataBuffer));

    const uint8_t *data = (const uint8_t *)malloc(length);

    dataBuffer->data = dataBuffer->position = data;
    dataBuffer->length = length;
    dataBuffer->next = NULL;

    if (buffer != NULL)
    {
        memcpy((void *)dataBuffer->data, buffer, length);
    }

    return dataBuffer;
}

static DataBuffer *initializeDataBuffer(size_t length)
{
    return initializeDataBuffer(NULL, length);
}

static size_t allocateForReceiving(void **buffer, size_t required)
{
    void *data = NULL;

    while (required > 0 && (data = malloc(required)) != NULL)
    {
        required >> 1;
    }

    *buffer = data;

    return required;
}

static err_t client_poll(void *data, tcp_pcb *controlBlock)
{
    PicoTcpClient *client = (PicoTcpClient *)data;
    return client->poll();
}

static err_t client_sent(void *data, tcp_pcb *controlBlock, uint16_t length)
{
    PicoTcpClient *client = (PicoTcpClient *)data;
    return client->sent(length);
}

static err_t client_receive(void *data, tcp_pcb *controlBlock, struct pbuf *payloadBuffer, err_t errorCode)
{
    PicoTcpClient *client = (PicoTcpClient *)data;
    return client->received(payloadBuffer, errorCode);
}

static void client_error(void *data, err_t errorCode)
{
    PicoTcpClient *client = (PicoTcpClient *)data;
    client->onError(errorCode);
}

static err_t client_connected(void *data, tcp_pcb *tcpControlBlock, err_t errorCode)
{
    PicoTcpClient *client = (PicoTcpClient *)data;
    return client->onConnected(errorCode);
}

err_t PicoTcpClient::writeDataBuffer(DataBuffer *buffer)
{
    DataBuffer *queue;
    int availableLength;
    err_t tcpCode;

    availableLength = tcp_sndbuf(tcpControlBlock);

    queue = buffer;

    while (availableLength > 0 && queue != NULL)
    {
        if (availableLength >= queue->length)
        {
            availableLength -= queue->length;
            tcpCode = tcp_write(tcpControlBlock, queue->position, queue->length, TCP_WRITE_FLAG_COPY);
            queue = queue->next;
        }
        else
        {
            queue->length -= availableLength;
            tcpCode = tcp_write(tcpControlBlock, queue->position, availableLength, TCP_WRITE_FLAG_COPY);
            queue->position += availableLength;
            availableLength = 0;
        }
    }

    switch (tcpCode)
    {
    case ERR_OK:
        /* code */
        tcp_output(tcpControlBlock);
        break;
    case ERR_MEM:
        /* code */
        break;
    case ERR_CONN:
        // TODO: Handle Not Connected
        isConnected = false;
        tcp_abort(tcpControlBlock);
        return ERR_ABRT;
        break;
    case ERR_ARG:
        // TODO: Handle Bad Args
        break;
    default:
        break;
    }

    return tcpCode;
}

int PicoTcpClient::sent(uint16_t length)
{
    DataBuffer *temp;
    uint16_t remaining = length;

    while (remaining > 0 && writeQueue != NULL)
    {
        if (remaining >= writeQueue->length)
        {
            remaining -= writeQueue->length;

            temp = writeQueue;
            writeQueue = writeQueue->next;

            free((void *)temp->data);
            free(temp);
        }
        else
        {
            writeQueue->length -= remaining;
            remaining = 0;
        }
    }

    if (writeQueue != NULL)
    {
        writeDataBuffer(writeQueue);
    }

    return ERR_OK;
}

err_t PicoTcpClient::received(struct pbuf *payloadBuffer, err_t errorCode)
{
    if (!payloadBuffer)
    {
        // TODO: Error
        isConnected = false;
        tcp_abort(tcpControlBlock);
        return ERR_ABRT;
    }
    cyw43_arch_lwip_check();
    if (payloadBuffer->tot_len > 0)
    {

        DataBuffer *received;

        received = initializeDataBuffer(payloadBuffer->tot_len);

        if (received == NULL)
        {
            return ERR_MEM;
        }

        pbuf_copy_partial(payloadBuffer, (void *)received->data, payloadBuffer->tot_len, 0);
        tcp_recved(tcpControlBlock, payloadBuffer->tot_len);

        availableData += payloadBuffer->tot_len;
        if (receiveQueue == NULL)
        {
            receiveQueue = received;
        }
        else
        {
            DataBuffer *tail = receiveQueue, *next = tail->next;
            while (next != NULL)
            {
                tail = next;
                next = tail->next;
            }

            tail->next = received;
        }
    }

    pbuf_free(payloadBuffer);
    return ERR_OK;
}

err_t PicoTcpClient::onConnected(int errorCode)
{
    if (errorCode != ERR_OK)
    {
        isConnected = false;
        DEBUG("Connect failed %d\n", errorCode);
        return errorCode;
    }
    DEBUG("Connect Success %d\n");
    isConnected = true;
    return ERR_OK;
}

void PicoTcpClient::onError(err_t errorCode)
{
    DEBUG("Error %d\n", errorCode);
    switch (errorCode)
    {
    case ERR_ABRT:
        /* code */
        break;
    case ERR_RST:
        /* code */
        break;

    default:
        break;
    }
    isConnected = false;
}

err_t PicoTcpClient::poll()
{
    return ERR_OK;
}

PicoTcpClient::PicoTcpClient()
{
}

PicoTcpClient::~PicoTcpClient()
{
    if (tcpControlBlock != NULL)
    {
        err_t errorCode = ERR_OK;

        tcp_arg(tcpControlBlock, NULL);
        tcp_poll(tcpControlBlock, NULL, 0);
        tcp_sent(tcpControlBlock, NULL);
        tcp_recv(tcpControlBlock, NULL);
        tcp_err(tcpControlBlock, NULL);

        errorCode = tcp_close(tcpControlBlock);
        if (errorCode != ERR_OK)
        {
            tcp_abort(tcpControlBlock);
        }
        tcpControlBlock = NULL;
    }
#if PICO_CYW43_ARCH_POLL
    cyw43_arch_poll();
#endif
}

int PicoTcpClient::connect(const char *hostname, uint16_t port)
{
    if (isConnected)
    {
        return BASE_ERROR;
    }

    isConfigured = false;
    if (!ip4addr_aton(hostname, &address))
    {
        // TODO: Error Handler
        return BASE_ERROR;
    }

    if (port < 0)
    {
        // TODO: Error Handler
        return BASE_ERROR;
    }

    this->port = port;

    isConfigured = true;
    if (!isConfigured)
    {
        // TODO: Not Configured
        return -1;
    }

    if (tcpControlBlock == NULL)
    {
        tcpControlBlock = tcp_new_ip_type(IP_GET_TYPE(&address));

        if (tcpControlBlock == NULL)
        {
            // TODO: Error Handler
            return BASE_ERROR;
        }

        tcp_arg(tcpControlBlock, this);
        tcp_poll(tcpControlBlock, client_poll, POLL_TIME_S * 2);
        tcp_sent(tcpControlBlock, client_sent);
        tcp_recv(tcpControlBlock, client_receive);
        tcp_err(tcpControlBlock, client_error);
    }

    err_t returnCode;

    cyw43_arch_lwip_begin();
    returnCode = tcp_connect(tcpControlBlock, &address, port, client_connected);
    cyw43_arch_lwip_end();

    return returnCode;
}

size_t PicoTcpClient::write(uint8_t data)
{
    return write(&data, 1);
}

size_t PicoTcpClient::write(const void *buffer, size_t length)
{
    int returnCode = 0;

    err_t tcpCode;

    if (writeQueue == NULL)
    {
        writeQueue = initializeDataBuffer(buffer, length);

        tcpCode = writeDataBuffer(writeQueue);
    }
    else
    {
        DataBuffer *tail = writeQueue, *next = tail->next;
        while (next != NULL)
        {
            tail = next;
            next = tail->next;
        }

        tail->next = initializeDataBuffer(buffer, length);
    }

    return 0;
}

int PicoTcpClient::available()
{
    return availableData;
}
/**
  ERR_OK         = 0,
  ERR_MEM        = -1,
  ERR_BUF        = -2,
  ERR_TIMEOUT    = -3,
  ERR_RTE        = -4,
  ERR_INPROGRESS = -5,
  ERR_VAL        = -6,
  ERR_WOULDBLOCK = -7,
  ERR_USE        = -8,
  ERR_ALREADY    = -9,
  ERR_ISCONN     = -10,
  ERR_CONN       = -11,
  ERR_IF         = -12,
  ERR_ABRT       = -13,
  ERR_RST        = -14,
  ERR_CLSD       = -15,
  ERR_ARG        = -16
} err_enum_t;
 */

int PicoTcpClient::read(void *buffer, size_t length)
{
    size_t remaining, dataRead;
    DataBuffer *queue, *temp;
    err_t tcpCode;

    uint8_t *position = (uint8_t *)buffer;

    queue = receiveQueue;

    dataRead = remaining = length < availableData ? length : availableData;

    while (remaining > 0 && queue != NULL)
    {
        if (remaining >= queue->length)
        {
            remaining -= queue->length;

            memcpy(position, queue->position, queue->length);
            position += queue->length;

            temp = queue;

            queue = queue->next;

            free((void *)temp->data);
            free(temp);
        }
        else
        {
            queue->length -= remaining;

            memcpy(position, queue->position, remaining);

            queue->position += remaining;
            remaining = 0;
        }
    }

    availableData -= dataRead;

    receiveQueue = queue;

    return dataRead;
}

void PicoTcpClient::stop()
{
    tcp_abort(tcpControlBlock);
}

uint8_t PicoTcpClient::connected()
{
    return isConnected;
}

void PicoTcpClient::sync()
{
#if PICO_CYW43_ARCH_POLL
    cyw43_arch_poll();
#endif
}
