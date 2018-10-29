#include <oni/rpc/pbconnection.h>
#include <oni/utils/logger.h>
#include <oni/utils/sys_wrappers.h>
#include <oni/utils/memory/allocator.h>
#include <oni/utils/kdlsym.h>
#include <oni/utils/ref.h>

#include <oni/messaging/messagemanager.h>

#include <protobuf-c/mirabuiltin.pb-c.h>
#include <oni/framework.h>
#include <oni/init/initparams.h>
#include <string.h>

void pbconnection_init(struct pbconnection_t* connection)
{
	void* (*memset)(void *s, int c, size_t n) = kdlsym(memset);
	void(*mtx_init)(struct mtx *m, const char *name, const char *type, int opts) = kdlsym(mtx_init);

	if (!connection)
		return;

	memset(connection, 0, sizeof(connection));

	mtx_init(&connection->lock, "", NULL, 0);

	connection->socket = -1;
	connection->thread = NULL;
	connection->running = false;
}

void pbconnection_thread(struct pbconnection_t* connection)
{
	void(*kthread_exit)(void) = kdlsym(kthread_exit);
	void(*_mtx_lock_flags)(struct mtx *m, int opts, const char *file, int line) = kdlsym(_mtx_lock_flags);
	void(*_mtx_unlock_flags)(struct mtx *m, int opts, const char *file, int line) = kdlsym(_mtx_unlock_flags);
	void* (*memset)(void *s, int c, size_t n) = kdlsym(memset);

	if (!connection)
		return;

	if (connection->socket < 0)
		return;

	// Grab the lock, this should prevent races from the spawner thread
	_mtx_lock_flags(&connection->lock, 0, __FILE__, __LINE__);

	WriteLog(LL_Info, "pbconnection thread created socket: (%d), addr: (%x), thread: (%p).", connection->socket, connection->address.sin_addr.s_addr, connection->thread);

	// Do not hold this lock
	_mtx_unlock_flags(&connection->lock, 0, __FILE__, __LINE__);

	WriteLog(LL_Warn, "here");

	connection->running = true;

	const uint32_t maxMessageSize = PAGE_SIZE * 2;
	uint8_t* data = NULL;
	while (connection->running)
	{
		WriteLog(LL_Warn, "here");

		uint32_t dataLength = 0;
		data = NULL;

		WriteLog(LL_Warn, "here");

		ssize_t result = krecv(connection->socket, &dataLength, sizeof(dataLength), 0);

		WriteLog(LL_Warn, "here");

		// Verify the recv worked successfully
		if (result <= 0)
		{
			WriteLog(LL_Error, "recv returned (%d).", result);
			goto disconnect;
		}

		WriteLog(LL_Warn, "here");

		// Verify the data we wanted was recv'd
		if (result != sizeof(dataLength))
		{
			WriteLog(LL_Error, "did not recv enough data, got (%d) wanted (%d).", result, sizeof(dataLength));
			goto disconnect;
		}

		WriteLog(LL_Warn, "here");

		// We will set PAGE_SIZE as our artifical max length
		if (dataLength > maxMessageSize)
		{
			WriteLog(LL_Error, "data length (%x) > max (%x).", dataLength, maxMessageSize);
			goto disconnect;
		}

		WriteLog(LL_Warn, "here");

		// Allocate some new data
		data = k_malloc(dataLength);
		if (!data)
		{
			WriteLog(LL_Error, "could not allocate message length (%x).", dataLength);
			goto disconnect;
		}

		WriteLog(LL_Warn, "here");

		// Zero our newly allocated buffer
		memset(data, 0, dataLength);

		WriteLog(LL_Warn, "here");

		// Recv our message buffer
		uint32_t bufferRecv = 0;
		result = krecv(connection->socket, data, dataLength, 0);
		if (result <= 0)
		{
			WriteLog(LL_Error, "could not recv message data (%d).", result);
			goto disconnect;
		}

		WriteLog(LL_Warn, "here");

		// Set our current buffer recv count
		bufferRecv = result;

		WriteLog(LL_Warn, "here");

		// Ensure that we get all of our data
		while (bufferRecv < dataLength)
		{
			WriteLog(LL_Warn, "here");

			// Calculate how much data we have left
			uint32_t amountLeft = dataLength - bufferRecv;
			if (amountLeft == 0)
				break;

			WriteLog(LL_Warn, "here");

			// Attempt to read the rest of the buffer
			result = krecv(connection->socket, data + bufferRecv, amountLeft, 0);

			WriteLog(LL_Warn, "here");

			// Check for errors
			if (result <= 0)
			{
				WriteLog(LL_Error, "could not recv the rest of data (%d).", result);
				goto disconnect;
			}

			WriteLog(LL_Warn, "here");

			// Add the new amount of data that we recv'd
			bufferRecv += result;
		}

		WriteLog(LL_Warn, "gKernelBase: %p", gKernelBase);
		WriteLog(LL_Warn, "payloadBase: %p", gInitParams->payloadBase);
		WriteLog(LL_Warn, "dataLength: %d, data: %p", dataLength, data);
		WriteLog(LL_Warn, "\n\n\n\n\n");


		// Decode the message header		
		uint8_t buf[1024];
		WriteLog(LL_Warn, "here");

		memset(buf, 0, sizeof(buf));

		WriteLog(LL_Warn, "here");

		memcpy(buf, data, dataLength);

		WriteLog(LL_Warn, "here");

		MessageHeader* header = message_header__unpack(NULL, dataLength, buf);

		WriteLog(LL_Warn, "here");

		if (!header)
		{
			WriteLog(LL_Error, "could not decode header\n");
			goto disconnect;
		}

		WriteLog(LL_Warn, "here");

		// Validate the message category
		MessageCategory category = header->category;

		WriteLog(LL_Warn, "here");

		if (category < MESSAGE_CATEGORY__NONE || category > MESSAGE_CATEGORY__MAX) // TODO: Add Max
		{
			WriteLog(LL_Error, "invalid category (%d).", category);
			goto disconnect;
		}

		WriteLog(LL_Warn, "here");

		// Validate the error code
		if (header->error != 0)
		{
			WriteLog(LL_Error, "error should not be set on requests (%d).", header->error);
			goto disconnect;
		}

		// Free our protobuf thing
		message_header__free_unpacked(header, NULL);

		WriteLog(LL_Warn, "here");

		// This creates a copy of the data
		struct ref_t* reference = ref_fromObject(data, dataLength);

		WriteLog(LL_Warn, "here");

		if (!reference)
		{
			WriteLog(LL_Error, "could not create new reference of data");
			goto disconnect;
		}

		WriteLog(LL_Warn, "here");

		// TODO: You will have to change endpoint to accept protobuf
		messagemanager_sendRequest(reference);

		WriteLog(LL_Warn, "here");

		// We no longer need to hold this reference
		ref_release(reference);
	}

	WriteLog(LL_Warn, "here");

disconnect:
	connection->running = false;

	WriteLog(LL_Warn, "here");

	// Validate everything and send the disconnect message, pbserver handles cleanup
	if (connection->server && connection->onClientDisconnect)
	{
		WriteLog(LL_Warn, "here");
		connection->onClientDisconnect(connection->server, connection);
	}

	WriteLog(LL_Warn, "here");

	kthread_exit();
}