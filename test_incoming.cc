#include "homa_incoming.h"
#include "mock.h"

class TestIncoming : public ::testing::Test {
public:   
    TestIncoming()
    {
        Mock::setUp();
    }
};

TEST_F(TestIncoming, read_basics) {
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    ASSERT_TRUE(msg);
    EXPECT_EQ(44U, msg->streamId.id);
    EXPECT_EQ(1000U, msg->messageLength);
    EXPECT_EQ(1051U, msg->baseLength);
}
TEST_F(TestIncoming, read_firsthomaRecvFails) {
    Mock::homaRecvErrors = 1;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    EXPECT_FALSE(msg);
    EXPECT_SUBSTR("gpr_log: Error in homa_recv:",
            Mock::log.c_str());
}
TEST_F(TestIncoming, read_firsthomaRecvTooShort) {
    Mock::homaRecvMsgLengths.push_back(4);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    EXPECT_FALSE(msg);
    EXPECT_SUBSTR("gpr_log: Homa message contained only 4 bytes",
            Mock::log.c_str());
}
TEST_F(TestIncoming, read_lengthsInconsistent) {
    Mock::homaRecvMsgLengths.push_back(1000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    EXPECT_FALSE(msg);
    EXPECT_SUBSTR("gpr_log: Bad message length 1000",
            Mock::log.c_str());
}
TEST_F(TestIncoming, read_tailhomaRecvFails) {
    Mock::homaRecvErrors = 2;
    Mock::homaRecvReturns.push_back(500);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    EXPECT_FALSE(msg);
    EXPECT_SUBSTR("gpr_log: Error in homa_recv for tail of id 333:",
            Mock::log.c_str());
}
TEST_F(TestIncoming, read_tailHasWrongLength) {
    Mock::homaRecvReturns.push_back(500);
    Mock::homaRecvReturns.push_back(500);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    EXPECT_FALSE(msg);
    EXPECT_SUBSTR("gpr_log: Tail of Homa message has wrong length",
            Mock::log.c_str());
}
TEST_F(TestIncoming, read_tailOK) {
    Mock::homaRecvReturns.push_back(500);
    Mock::homaRecvReturns.push_back(551);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    ASSERT_TRUE(msg);
    EXPECT_LT(100U, msg->tail.size());
}

TEST_F(TestIncoming, copyOut) {
    int destroyCounter = 0;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    msg->destroyCounter = &destroyCounter;
    msg->baseLength = 500;
    msg->tail.resize(1000);
    Mock::fillData(&msg->hdr, 500, 0);
    Mock::fillData(msg->tail.data(), 1000, 1000);
    
    // First block is in the static part of the message.
    char buffer[40];
    msg->copyOut(buffer, 460, sizeof(buffer));
    Mock::logData("; ", buffer, sizeof(buffer));
    EXPECT_STREQ("460-499", Mock::log.c_str());
    
    // Second slice is entirely in the tail of the message.
    Mock::log.clear();
    msg->copyOut(buffer, 500, sizeof(buffer));
    Mock::logData("; ", buffer, sizeof(buffer));
    EXPECT_STREQ("1000-1039", Mock::log.c_str());
    
    // Third slice straddles the boundary.
    Mock::log.clear();
    msg->copyOut(buffer, 484, sizeof(buffer));
    Mock::logData("; ", buffer, sizeof(buffer));
    EXPECT_STREQ("484-499 1000-1023", Mock::log.c_str());\
}

TEST_F(TestIncoming, getStaticSlice) {
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    msg->baseLength = 500;
    Mock::fillData(&msg->hdr, 500, 0);
    
    // First slice is small enough to be stored internally.
    grpc_slice slice1 = msg->getStaticSlice(60, 8, arena);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice1), GRPC_SLICE_LENGTH(slice1));
    EXPECT_STREQ("60-67", Mock::log.c_str());
    EXPECT_EQ(nullptr, slice1.refcount);
    
    // Second slice is allocated in the arena.
    Mock::log.clear();
    grpc_slice slice2 = msg->getStaticSlice(100, 200, arena);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice2), GRPC_SLICE_LENGTH(slice2));
    EXPECT_STREQ("100-299", Mock::log.c_str());
    EXPECT_EQ(&grpc_core::kNoopRefcount, slice2.refcount);
    
    arena->Destroy();
}

TEST_F(TestIncoming, getSlice) {
    int destroyCounter = 0;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    msg->destroyCounter = &destroyCounter;
    msg->baseLength = 500;
    msg->tail.resize(1000);
    Mock::fillData(&msg->hdr, 500, 0);
    Mock::fillData(msg->tail.data(), 1000, 1000);
    
    // First slice is in the static part of the message.
    grpc_slice slice1 = msg->getSlice(440, 60);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice1), GRPC_SLICE_LENGTH(slice1));
    EXPECT_STREQ("440-499", Mock::log.c_str());
    
    // Second slice is entirely in the tail of the message.
    Mock::log.clear();
    grpc_slice slice2 = msg->getSlice(500, 100);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice2), GRPC_SLICE_LENGTH(slice2));
    EXPECT_STREQ("1000-1099", Mock::log.c_str());
    
    // Third slice straddles the boundary.
    Mock::log.clear();
    grpc_slice slice3 = msg->getSlice(420,200);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice3), GRPC_SLICE_LENGTH(slice3));
    EXPECT_STREQ("420-499 1000-1119", Mock::log.c_str());
    
    // Now make sure that the reference counting worked correctly.
    EXPECT_EQ(0, destroyCounter);
    msg.reset(nullptr);
    EXPECT_EQ(0, destroyCounter);
    grpc_slice_unref(slice3);
    EXPECT_EQ(0, destroyCounter);
    grpc_slice_unref(slice2);
    EXPECT_EQ(0, destroyCounter);
    grpc_slice_unref(slice1);
    EXPECT_EQ(1, destroyCounter);
}

TEST_F(TestIncoming, deserializeMetadata_basics) {
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    ASSERT_TRUE(msg);
    int destroyCounter = 0;
    msg->destroyCounter = &destroyCounter;
    size_t length = msg->addMetadata(75, 100,
            "name1", "value1", 100,
            "name2", "value2", 100,
            "n3", "abcdefghijklmnop", 100, nullptr);
    grpc_metadata_batch batch;
    grpc_metadata_batch_init(&batch);
    msg->deserializeMetadata(75, length, &batch, arena);
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata name1: value1 (24); "
            "metadata name2: value2 (24); "
            "metadata n3: abcdefghijklmnop (24)", Mock::log.c_str());
    msg.reset(nullptr);
    EXPECT_EQ(1, destroyCounter);
    grpc_metadata_batch_destroy(&batch);
    arena->Destroy();
}
TEST_F(TestIncoming, deserializeMetadata_metadataOverrunsSpace) {
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75, 100,
            "name1", "value1", 100,
            "name2", "value2", 100,
            "n3", "abcdefghijklmnop", 100, nullptr);
    grpc_metadata_batch batch;
    grpc_metadata_batch_init(&batch);
    msg->deserializeMetadata(75, length-1, &batch, arena);
    EXPECT_STREQ("gpr_log: Metadata format error: key (2 bytes) and "
            "value (16 bytes) exceed remaining space (17 bytes)",
            Mock::log.c_str());
    grpc_metadata_batch_destroy(&batch);
    arena->Destroy();
}
TEST_F(TestIncoming, deserializeMetadata_useCallout) {
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75, 1000,
            "name1", "value1", GRPC_BATCH_PATH,
            "name2", "value2", 100, nullptr);
    grpc_metadata_batch batch;
    grpc_metadata_batch_init(&batch);
    msg->deserializeMetadata(75, length, &batch, arena);
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata :path: value1 (0); metadata name2: value2 (24)",
            Mock::log.c_str());
    grpc_metadata_batch_destroy(&batch);
    arena->Destroy();
}
TEST_F(TestIncoming, deserializeMetadata_valueMustBeManaged) {
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    ASSERT_TRUE(msg);
    int destroyCounter = 0;
    msg->destroyCounter = &destroyCounter;
    size_t length = msg->addMetadata(75, 1000,
            "name1", "value1", 100,
            "name2", "0123456789abcdefghij", 100, nullptr);
    grpc_metadata_batch batch;
    grpc_metadata_batch_init(&batch);
    msg->maxStaticMdLength = 10;
    msg->deserializeMetadata(75, length, &batch, arena);
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata name1: value1 (24); "
            "metadata name2: 0123456789abcdefghij (24)",
            Mock::log.c_str());
    msg.reset(nullptr);
    EXPECT_EQ(0, destroyCounter);
    grpc_metadata_batch_destroy(&batch);
    EXPECT_EQ(1, destroyCounter);
    arena->Destroy();
}
TEST_F(TestIncoming, deserializeMetadata_incompleteHeader) {
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75, 100,
            "name1", "value1", 100,
            "name2", "value2", 100,
            "n3", "abcdefghijklmnop", 100, nullptr);
    grpc_metadata_batch batch;
    grpc_metadata_batch_init(&batch);
    msg->deserializeMetadata(75, length+3, &batch, arena);
    EXPECT_SUBSTR("only 3 bytes available", Mock::log.c_str());
    grpc_metadata_batch_destroy(&batch);
    arena->Destroy();
}

TEST_F(TestIncoming, getBytes) {
    struct Bytes16 {uint8_t data[16];};
    Bytes16 buffer;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5);
    msg->baseLength = 500;
    msg->tail.resize(1000);
    Mock::fillData(&msg->hdr, 500, 0);
    Mock::fillData(msg->tail.data(), 1000, 1000);
    
    // First extraction fits in initial data.
    Bytes16 *p = msg->getBytes<Bytes16>(484, &buffer);
    Mock::logData("; ", p->data, 16);
    EXPECT_STREQ("484-499", Mock::log.c_str());
    
    // Second extraction straddles the initial data and the tail.
    Mock::log.clear();
    p = msg->getBytes<Bytes16>(496, &buffer);
    Mock::logData("; ", p->data, 16);
    EXPECT_STREQ("496-499 1000-1011", Mock::log.c_str());
    
    // Third extraction is entirely in the tail.
    Mock::log.clear();
    p = msg->getBytes<Bytes16>(500, &buffer);
    Mock::logData("; ", p->data, 16);
    EXPECT_STREQ("1000-1015", Mock::log.c_str());
}