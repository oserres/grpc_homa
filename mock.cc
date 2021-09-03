#include <linux/types.h>

#include <cstdarg>

#include "homa.h"
#include "mock.h"

/* This file provides simplified substitutes for various features, in
 * order to enable better unit testing.
 */

int Mock::homaRecvErrors = 0;
int Mock::homaReplyvErrors = 0;
int Mock::homaSendvErrors = 0;

std::deque<Wire::Header>    Mock::homaRecvHeaders;
std::deque<ssize_t>         Mock::homaRecvMsgLengths;
std::deque<ssize_t>         Mock::homaRecvReturns;
std::string                 Mock::log;

/**
 * Determines whether a method should simulate an error return.
 * \param errorMask
 *      Address of a variable containing a bit mask, indicating which of the
 *      next calls should result in errors. The variable is modified by
 *      this function.
 * \return
 *      Zero means the function should behave normally; 1 means return
 *      an error.
 */
int Mock::checkError(int *errorMask)
{
	int result = *errorMask & 1;
	*errorMask = *errorMask >> 1;
	return result;
}

/**
 * Fill in a block of memory with predictable values that can be checked
 * later by unit_log_data.
 * \param data
 *      Address of first byte of data.
 * \param length
 *      Total amount of data, in bytes.
 * \param firstValue
 *      Value to store in first 4 bytes of data. Each successive 4 bytes
 *      of data will have a value 4 greater than the previous.
 */
void Mock::fillData(void *data, int length, int firstValue)
{ 
	int i;
    uint8_t *p = static_cast<uint8_t *>(data);
	for (i = 0; i <= length-4; i += 4) {
		*reinterpret_cast<int32_t *>(p + i) = firstValue + i;
	}
	
	/* Fill in extra bytes with a special value. */
	for ( ; i < length; i += 1) {
		p[i] = 0xaa;
	}
}

/**
 * This method captures calls to gpr_log and logs the information in
 * the Mock log.
 * \param args
 *      Information from the original gpr_log call.
 */
void Mock::gprLog(gpr_log_func_args* args)
{
    logPrintf("; ", "gpr_log: %s", args->message);
}

/**
 * Log information that describes a block of data previously encoded
 * with fillData.
 * \param separator
 *      Initial separator in the log.
 * \param data
 *      Address of first byte of data. Should have been encoded with
 *      fillData.
 * \param length
 *      Total amount of data, in bytes.
 */
void Mock::logData(const char *separator, void *data, int length)
{
	int i, rangeStart, expectedNext;
    uint8_t* p = static_cast<uint8_t *>(data);
	if (length == 0) {
		logPrintf(separator, "empty block");
		return;
	}
	if (length >= 4)
		rangeStart = *reinterpret_cast<int32_t *>(p);
	expectedNext = rangeStart;
	for (i = 0; i <= length-4; i += 4) {
		int current = *reinterpret_cast<int32_t *>(p + i);
		if (current != expectedNext) {
			logPrintf(separator, "%d-%d", rangeStart, expectedNext-1);
			separator = " ";
			rangeStart = current;
		}
		expectedNext = current+4;
	}
	logPrintf(separator, "%d-%d", rangeStart, expectedNext-1);
	separator = " ";
	
	for ( ; i < length; i += 1) {
		logPrintf(separator, "0x%x", p[i]);
		separator = " ";
	}
}

/**
 * Print information about a batch of metadata to Mock::log.
 * \param separator
 *      Log separator string (before each metadata entry).
 * \param batch
 *      Metadata to print.
 */
void Mock::logMetadata(const char *separator, const grpc_metadata_batch *batch)
{
    for (grpc_linked_mdelem* md = batch->list.head; md != nullptr;
            md = md->next) {
        const grpc_slice& key = GRPC_MDKEY(md->md);
        const grpc_slice& value = GRPC_MDVALUE(md->md);
        logPrintf(separator, "metadata %.*s: %.*s (%d)",
                GRPC_SLICE_LENGTH(key), GRPC_SLICE_START_PTR(key),
                GRPC_SLICE_LENGTH(value), GRPC_SLICE_START_PTR(value),
                GRPC_BATCH_INDEX_OF(key));
    }
}

/**
 * Append information to the test log.
 * \param separator
 *      If non-NULL, and if the log is non-empty, this string is added to
 *      the log before the new message.
 * \param format
 *      Standard printf-style format string.
 * \param ap
 *      Additional arguments as required by @format.
 */
void Mock::logPrintf(const char *separator, const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	
	if (!log.empty() && (separator != NULL))
		log.append(separator);
	    
	// We're not really sure how big of a buffer will be necessary.
	// Try 1K, if not the return value will tell us how much is necessary.
	int bufSize = 1024;
	while (true) {
		char buf[bufSize];
		// vsnprintf trashes the va_list, so copy it first
		va_list aq;
		__va_copy(aq, ap);
		int length = vsnprintf(buf, bufSize, format, aq);
		if (length < bufSize) {
			log.append(buf, length);
			break;
		}
		bufSize = length + 1;
	}
	va_end(ap);
}

/**
 * Invoked at the start of each unit test to reset all mocking information.
 */
void Mock::setUp(void)
{
    grpc_init();
    gpr_set_log_function(gprLog);
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_ERROR);
    
    homaRecvErrors = 0;
    homaReplyvErrors = 0;
    homaSendvErrors = 0;
    
    homaRecvHeaders.clear();
    homaRecvMsgLengths.clear();
    homaRecvReturns.clear();
    log.clear();
}

/**
 * Used in EXPECT_SUBSTR to fail a gtest test case if a given string
 * doesn't contain a given substring.
 * \param s
 *      Test output.
 * \param substring
 *      Substring expected to appear somewhere in s.
 * \return
 *      A value that can be tested with EXPECT_TRUE.
 */
::testing::AssertionResult
Mock::substr(const std::string& s, const std::string& substring)
{
    if (s.find(substring) == s.npos) {
        char buffer[1000];
        snprintf(buffer, sizeof(buffer), "Substring '%s' not present in '%s'",
                substring.c_str(), s.c_str());
        std::string message(buffer);
        return ::testing::AssertionFailure() << message;
    }
    return ::testing::AssertionSuccess();
}

ssize_t homa_recv(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *srcAddr, size_t *addrlen, uint64_t *id,
        size_t *msglen)
{
    Wire::Header *h = static_cast<Wire::Header *>(buf);
    if (Mock::checkError(&Mock::homaRecvErrors)) {
        errno = EIO;
        return -1;
    }
    *id = 333;
    if (Mock::homaRecvHeaders.empty()) {
        new (buf) Wire::Header(44, 0, 10, 20, 1000);
    } else {
        *(reinterpret_cast<Wire::Header *>(buf)) = Mock::homaRecvHeaders.front();
        Mock::homaRecvHeaders.pop_front();
    }
    size_t length;
    if (Mock::homaRecvMsgLengths.empty()) {
        length = sizeof(*h) + ntohl(h->initMdBytes) + ntohl(h->messageBytes)
            + ntohl(h->trailMdBytes);
    } else {
        length = Mock::homaRecvMsgLengths.front();
        Mock::homaRecvMsgLengths.pop_front();
    }
    if (msglen) {
        *msglen = length;
    }
    if (Mock::homaRecvReturns.empty()) {
        return length;
    }
    ssize_t result = Mock::homaRecvReturns.front();
    Mock::homaRecvReturns.pop_front();
    return result;
}

ssize_t homa_replyv(int sockfd, const struct iovec *iov, int iovcnt,
        const struct sockaddr *dest_addr, size_t addrlen, uint64_t id)
{
    size_t totalLength = 0;
    for (int i = 0; i < iovcnt; i++) {
        totalLength += iov[i].iov_len;
    }
    Mock::logPrintf("; ", "homa_replyv: %d iovecs, %lu bytes", iovcnt,
            totalLength);
    if (Mock::checkError(&Mock::homaReplyvErrors)) {
        errno = EIO;
        return -1;
    }
    return totalLength;
}

int homa_sendv(int sockfd, const struct iovec *iov, int iovcnt,
        const struct sockaddr *dest_addr, size_t addrlen, uint64_t *id)
{
    size_t totalLength = 0;
    for (int i = 0; i < iovcnt; i++) {
        totalLength += iov[i].iov_len;
    }
    Mock::logPrintf("; ", "homa_sendv: %d iovecs, %lu bytes", iovcnt,
            totalLength);
    if (Mock::checkError(&Mock::homaSendvErrors)) {
        errno = EIO;
        return -1;
    }
    return totalLength;
}