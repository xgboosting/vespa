// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/document/base/testdocman.h>
#include <vespa/document/datatype/tensor_data_type.h>
#include <vespa/document/fieldvalue/fieldvalues.h>
#include <vespa/document/repo/configbuilder.h>
#include <vespa/document/repo/documenttyperepo.h>
#include <vespa/document/serialization/vespadocumentserializer.h>
#include <vespa/document/update/addvalueupdate.h>
#include <vespa/document/update/arithmeticvalueupdate.h>
#include <vespa/document/update/assignvalueupdate.h>
#include <vespa/document/update/clearvalueupdate.h>
#include <vespa/document/update/documentupdate.h>
#include <vespa/document/update/documentupdateflags.h>
#include <vespa/document/update/fieldupdate.h>
#include <vespa/document/update/mapvalueupdate.h>
#include <vespa/document/update/removevalueupdate.h>
#include <vespa/document/update/tensor_add_update.h>
#include <vespa/document/update/tensor_modify_update.h>
#include <vespa/document/update/tensor_remove_update.h>
#include <vespa/document/update/valueupdate.h>
#include <vespa/document/util/bytebuffer.h>
#include <vespa/eval/tensor/default_tensor_engine.h>
#include <vespa/eval/tensor/tensor.h>
#include <vespa/vespalib/objects/nbostream.h>
#include <vespa/vespalib/util/exception.h>
#include <vespa/vespalib/util/exceptions.h>

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

using namespace document::config_builder;
using vespalib::eval::TensorSpec;
using vespalib::tensor::DefaultTensorEngine;
using vespalib::tensor::Tensor;
using vespalib::nbostream;

namespace document {

namespace {

ByteBuffer::UP serializeHEAD(const DocumentUpdate & update)
{
    nbostream stream;
    VespaDocumentSerializer serializer(stream);
    serializer.writeHEAD(update);
    ByteBuffer::UP retVal(new ByteBuffer(stream.size()));
    retVal->putBytes(stream.peek(), stream.size());
    return retVal;
}

ByteBuffer::UP serialize42(const DocumentUpdate & update)
{
    nbostream stream;
    VespaDocumentSerializer serializer(stream);
    serializer.write42(update);
    ByteBuffer::UP retVal(new ByteBuffer(stream.size()));
    retVal->putBytes(stream.peek(), stream.size());
    return retVal;
}

nbostream serialize(const ValueUpdate & update)
{
    nbostream stream;
    VespaDocumentSerializer serializer(stream);
    serializer.write(update);
    return stream;
}

nbostream serialize(const FieldUpdate & update)
{
    nbostream stream;
    VespaDocumentSerializer serializer(stream);
    serializer.write(update);
    return stream;
}

template<typename UpdateType>
void testRoundtripSerialize(const UpdateType& update, const DataType &type) {
    try{
        DocumentTypeRepo repo;
        nbostream stream = serialize(update);
        std::unique_ptr<UpdateType> copy(dynamic_cast<UpdateType*>(ValueUpdate::createInstance(repo, type, stream).release()));
        EXPECT_EQ(update, *copy);
    } catch (std::exception& e) {
            std::cerr << "Failed while processing update " << update << "\n";
    throw;
    }
}

}

TEST(DocumentUpdateTest, testSimpleUsage)
{
    DocumenttypesConfigBuilderHelper builder;
    builder.document(42, "test",
                     Struct("test.header").addField("bytef", DataType::T_BYTE).addField("intf", DataType::T_INT),
                     Struct("test.body").addField("intarr", Array(DataType::T_INT)));
    DocumentTypeRepo repo(builder.config());
    const DocumentType* docType(repo.getDocumentType("test"));
    const DataType *arrayType = repo.getDataType(*docType, "Array<Int>");

        // Test that primitive value updates can be serialized
    testRoundtripSerialize(ClearValueUpdate(), *DataType::INT);
    testRoundtripSerialize(AssignValueUpdate(IntFieldValue(1)), *DataType::INT);
    testRoundtripSerialize(ArithmeticValueUpdate(ArithmeticValueUpdate::Div, 4.3), *DataType::FLOAT);
    testRoundtripSerialize(AddValueUpdate(IntFieldValue(1), 4), *arrayType);
    testRoundtripSerialize(RemoveValueUpdate(IntFieldValue(1)), *arrayType);

    FieldUpdate fieldUpdate(docType->getField("intf"));
    fieldUpdate.addUpdate(AssignValueUpdate(IntFieldValue(1)));
    nbostream stream = serialize(fieldUpdate);
    FieldUpdate fieldUpdateCopy(repo, *docType, stream);
    EXPECT_EQ(fieldUpdate, fieldUpdateCopy);

        // Test that a document update can be serialized
    DocumentUpdate docUpdate(repo, *docType, DocumentId("doc::testdoc"));
    docUpdate.addUpdate(fieldUpdateCopy);
    ByteBuffer::UP docBuf = serializeHEAD(docUpdate);
    docBuf->flip();
    auto docUpdateCopy(DocumentUpdate::createHEAD(repo, nbostream(docBuf->getBufferAtPos(), docBuf->getRemaining())));

        // Create a test document
    Document doc(*docType, DocumentId("doc::testdoc"));
    doc.set("bytef", 0);
    doc.set("intf", 5);
    ArrayFieldValue array(*arrayType);
    array.add(IntFieldValue(3));
    array.add(IntFieldValue(7));
    doc.setValue("intarr", array);

        // Verify that we can apply simple updates to it
    {
        Document updated(doc);
        DocumentUpdate upd(repo, *docType, DocumentId("doc::testdoc"));
        upd.addUpdate(FieldUpdate(docType->getField("intf")).addUpdate(ClearValueUpdate()));
        upd.applyTo(updated);
        EXPECT_NE(doc, updated);
        EXPECT_FALSE(updated.getValue("intf"));
    }
    {
        Document updated(doc);
        DocumentUpdate upd(repo, *docType, DocumentId("doc::testdoc"));
        upd.addUpdate(FieldUpdate(docType->getField("intf")).addUpdate(AssignValueUpdate(IntFieldValue(15))));
        upd.applyTo(updated);
        EXPECT_NE(doc, updated);
        EXPECT_EQ(15, updated.getValue("intf")->getAsInt());
    }
    {
        Document updated(doc);
        DocumentUpdate upd(repo, *docType, DocumentId("doc::testdoc"));
        upd.addUpdate(FieldUpdate(docType->getField("intf")).addUpdate(ArithmeticValueUpdate(ArithmeticValueUpdate::Add, 15)));
        upd.applyTo(updated);
        EXPECT_NE(doc, updated);
        EXPECT_EQ(20, updated.getValue("intf")->getAsInt());
    }
    {
        Document updated(doc);
        DocumentUpdate upd(repo, *docType, DocumentId("doc::testdoc"));
        upd.addUpdate(FieldUpdate(docType->getField("intarr")).addUpdate(AddValueUpdate(IntFieldValue(4))));
        upd.applyTo(updated);
        EXPECT_NE(doc, updated);
        std::unique_ptr<ArrayFieldValue> val(dynamic_cast<ArrayFieldValue*>(updated.getValue("intarr").release()));
        ASSERT_EQ(size_t(3), val->size());
        EXPECT_EQ(4, (*val)[2].getAsInt());
    }
    {
        Document updated(doc);
        DocumentUpdate upd(repo, *docType, DocumentId("doc::testdoc"));
        upd.addUpdate(FieldUpdate(docType->getField("intarr")).addUpdate(RemoveValueUpdate(IntFieldValue(3))));
        upd.applyTo(updated);
        EXPECT_NE(doc, updated);
        std::unique_ptr<ArrayFieldValue> val(dynamic_cast<ArrayFieldValue*>(updated.getValue("intarr").release()));
        ASSERT_EQ(size_t(1), val->size());
        EXPECT_EQ(7, (*val)[0].getAsInt());
    }
    {
        Document updated(doc);
        DocumentUpdate upd(repo, *docType, DocumentId("doc::testdoc"));
        upd.addUpdate(FieldUpdate(docType->getField("bytef"))
                              .addUpdate(ArithmeticValueUpdate(ArithmeticValueUpdate::Add, 15)));
        upd.applyTo(updated);
        EXPECT_NE(doc, updated);
        EXPECT_EQ(15, (int) updated.getValue("bytef")->getAsByte());
    }
}

TEST(DocumentUpdateTest, testClearField)
{
    // Create a document.
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    doc->setValue(doc->getField("headerval"), IntFieldValue(4));
    EXPECT_EQ(4, doc->getValue("headerval")->getAsInt());

    // Apply an update.
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(doc->getField("headerval")).addUpdate(AssignValueUpdate()))
        .applyTo(*doc);
    EXPECT_FALSE(doc->getValue("headerval"));
}

TEST(DocumentUpdateTest, testUpdateApplySingleValue)
{
    // Create a document.
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    doc->setValue(doc->getField("headerval"), IntFieldValue(4));
    EXPECT_EQ(4, doc->getValue("headerval")->getAsInt());

    // Apply an update.
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(doc->getField("headerval")).addUpdate(AssignValueUpdate(IntFieldValue(9))))
        .applyTo(*doc);
    EXPECT_EQ(9, doc->getValue("headerval")->getAsInt());
}

TEST(DocumentUpdateTest, testUpdateArray)
{
    // Create a document.
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    EXPECT_EQ((document::FieldValue*)NULL, doc->getValue(doc->getField("tags")).get());

    // Assign array field.
    ArrayFieldValue myarray(doc->getType().getField("tags").getDataType());
    myarray.add(StringFieldValue("foo"));
	myarray.add(StringFieldValue("bar"));

    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(doc->getField("tags")).addUpdate(AssignValueUpdate(myarray)))
        .applyTo(*doc);
    auto fval1(doc->getAs<ArrayFieldValue>(doc->getField("tags")));
    ASSERT_EQ((size_t) 2, fval1->size());
    EXPECT_EQ(std::string("foo"), std::string((*fval1)[0].getAsString()));
    EXPECT_EQ(std::string("bar"), std::string((*fval1)[1].getAsString()));

    // Append array field
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(doc->getField("tags"))
                   .addUpdate(AddValueUpdate(StringFieldValue("another")))
                   .addUpdate(AddValueUpdate(StringFieldValue("tag"))))
        .applyTo(*doc);
    std::unique_ptr<ArrayFieldValue>
        fval2(doc->getAs<ArrayFieldValue>(doc->getField("tags")));
    ASSERT_EQ((size_t) 4, fval2->size());
    EXPECT_EQ(std::string("foo"), std::string((*fval2)[0].getAsString()));
    EXPECT_EQ(std::string("bar"), std::string((*fval2)[1].getAsString()));
    EXPECT_EQ(std::string("another"), std::string((*fval2)[2].getAsString()));
    EXPECT_EQ(std::string("tag"), std::string((*fval2)[3].getAsString()));

    // Append single value.
    ASSERT_THROW(
        DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
            .addUpdate(FieldUpdate(doc->getField("tags"))
                       .addUpdate(AssignValueUpdate(StringFieldValue("THROW MEH!"))))
            .applyTo(*doc),
        std::exception) << "Expected exception when assigning a string value to an array field.";

    // Remove array field.
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(doc->getField("tags"))
                   .addUpdate(RemoveValueUpdate(StringFieldValue("foo")))
                   .addUpdate(RemoveValueUpdate(StringFieldValue("tag"))))
        .applyTo(*doc);
    auto fval3(doc->getAs<ArrayFieldValue>(doc->getField("tags")));
    ASSERT_EQ((size_t) 2, fval3->size());
    EXPECT_EQ(std::string("bar"), std::string((*fval3)[0].getAsString()));
    EXPECT_EQ(std::string("another"), std::string((*fval3)[1].getAsString()));

    // Remove array from array.
    ArrayFieldValue myarray2(doc->getType().getField("tags").getDataType());
    myarray2.add(StringFieldValue("foo"));
    myarray2.add(StringFieldValue("bar"));
    ASSERT_THROW(
        DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
            .addUpdate(FieldUpdate(doc->getField("tags"))
                       .addUpdate(RemoveValueUpdate(myarray2)))
            .applyTo(*doc),
        std::exception) << "Expected exception when removing an array from a string array.";
}

TEST(DocumentUpdateTest, testUpdateWeightedSet)
{
    // Create a test document
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    const Field& field(doc->getType().getField("stringweightedset"));
    EXPECT_EQ((FieldValue*) 0, doc->getValue(field).get());
	
    // Assign weightedset field
    WeightedSetFieldValue wset(field.getDataType());
    wset.add(StringFieldValue("foo"), 3);
    wset.add(StringFieldValue("bar"), 14);
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field).addUpdate(AssignValueUpdate(wset)))
        .applyTo(*doc);
    auto fval1(doc->getAs<WeightedSetFieldValue>(field));
    ASSERT_EQ((size_t) 2, fval1->size());
    EXPECT_TRUE(fval1->contains(StringFieldValue("foo")));
    EXPECT_NE(fval1->find(StringFieldValue("foo")), fval1->end());
    EXPECT_EQ(3, fval1->get(StringFieldValue("foo"), 0));
    EXPECT_TRUE(fval1->contains(StringFieldValue("bar")));
    EXPECT_NE(fval1->find(StringFieldValue("bar")), fval1->end());
    EXPECT_EQ(14, fval1->get(StringFieldValue("bar"), 0));

    // Do a second assign
    WeightedSetFieldValue wset2(field.getDataType());
    wset2.add(StringFieldValue("foo"), 16);
    wset2.add(StringFieldValue("bar"), 24);
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field)
                   .addUpdate(AssignValueUpdate(wset2)))
        .applyTo(*doc);
    auto fval2(doc->getAs<WeightedSetFieldValue>(field));
    ASSERT_EQ((size_t) 2, fval2->size());
    EXPECT_TRUE(fval2->contains(StringFieldValue("foo")));
    EXPECT_NE(fval2->find(StringFieldValue("foo")), fval1->end());
    EXPECT_EQ(16, fval2->get(StringFieldValue("foo"), 0));
    EXPECT_TRUE(fval2->contains(StringFieldValue("bar")));
    EXPECT_NE(fval2->find(StringFieldValue("bar")), fval1->end());
    EXPECT_EQ(24, fval2->get(StringFieldValue("bar"), 0));

    // Append weighted field
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field)
                   .addUpdate(AddValueUpdate(StringFieldValue("foo")).setWeight(3))
                   .addUpdate(AddValueUpdate(StringFieldValue("too")).setWeight(14)))
        .applyTo(*doc);
    std::unique_ptr<WeightedSetFieldValue>
        fval3(doc->getAs<WeightedSetFieldValue>(field));
    ASSERT_EQ((size_t) 3, fval3->size());
    EXPECT_TRUE(fval3->contains(StringFieldValue("foo")));
    EXPECT_EQ(3, fval3->get(StringFieldValue("foo"), 0));
    EXPECT_TRUE(fval3->contains(StringFieldValue("bar")));
    EXPECT_EQ(24, fval3->get(StringFieldValue("bar"), 0));
    EXPECT_TRUE(fval3->contains(StringFieldValue("too")));
    EXPECT_EQ(14, fval3->get(StringFieldValue("too"), 0));

    // Remove weighted field
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field)
                   .addUpdate(RemoveValueUpdate(StringFieldValue("foo")))
                   .addUpdate(RemoveValueUpdate(StringFieldValue("too"))))
        .applyTo(*doc);
    auto fval4(doc->getAs<WeightedSetFieldValue>(field));
    ASSERT_EQ((size_t) 1, fval4->size());
    EXPECT_FALSE(fval4->contains(StringFieldValue("foo")));
    EXPECT_TRUE(fval4->contains(StringFieldValue("bar")));
    EXPECT_EQ(24, fval4->get(StringFieldValue("bar"), 0));
    EXPECT_FALSE(fval4->contains(StringFieldValue("too")));
}

namespace {

struct WeightedSetAutoCreateFixture
{
    DocumentTypeRepo repo;
    const DocumentType* docType;
    Document doc;
    const Field& field;
    DocumentUpdate update;

    ~WeightedSetAutoCreateFixture();
    WeightedSetAutoCreateFixture();

    void applyUpdateToDocument() {
        update.applyTo(doc);
    }

    static DocumenttypesConfig makeConfig() {
        DocumenttypesConfigBuilderHelper builder;
        // T_TAG is an alias for a weighted set with create-if-non-existing
        // and remove-if-zero attributes set. Attempting to explicitly create
        // a field matching those characteristics will in fact fail with a
        // redefinition error.
        builder.document(42, "test", Struct("test.header").addField("strwset", DataType::T_TAG), Struct("test.body"));
        return builder.config();
    }
};

WeightedSetAutoCreateFixture::~WeightedSetAutoCreateFixture() = default;
WeightedSetAutoCreateFixture::WeightedSetAutoCreateFixture()
    : repo(makeConfig()),
      docType(repo.getDocumentType("test")),
      doc(*docType, DocumentId("doc::testdoc")),
      field(docType->getField("strwset")),
      update(repo, *docType, DocumentId("doc::testdoc"))
{
    update.addUpdate(FieldUpdate(field)
                             .addUpdate(MapValueUpdate(StringFieldValue("foo"),
                                                       ArithmeticValueUpdate(ArithmeticValueUpdate::Add, 1))));
}
} // anon ns

TEST(DocumentUpdateTest, testIncrementNonExistingAutoCreateWSetField)
{
    WeightedSetAutoCreateFixture fixture;

    fixture.applyUpdateToDocument();

    std::unique_ptr<WeightedSetFieldValue> ws(
            fixture.doc.getAs<WeightedSetFieldValue>(fixture.field));
    ASSERT_EQ(size_t(1), ws->size());
    EXPECT_TRUE(ws->contains(StringFieldValue("foo")));
    EXPECT_EQ(1, ws->get(StringFieldValue("foo"), 0));
}

TEST(DocumentUpdateTest, testIncrementExistingWSetField)
{
    WeightedSetAutoCreateFixture fixture;
    {
        WeightedSetFieldValue wset(fixture.field.getDataType());
        wset.add(StringFieldValue("bar"), 14);
        fixture.doc.setValue(fixture.field, wset);
    }
    fixture.applyUpdateToDocument();

    auto ws(fixture.doc.getAs<WeightedSetFieldValue>(fixture.field));
    ASSERT_EQ(size_t(2), ws->size());
    EXPECT_TRUE(ws->contains(StringFieldValue("foo")));
    EXPECT_EQ(1, ws->get(StringFieldValue("foo"), 0));
}

TEST(DocumentUpdateTest, testIncrementWithZeroResultWeightIsRemoved)
{
    WeightedSetAutoCreateFixture fixture;
    fixture.update.addUpdate(FieldUpdate(fixture.field)
            .addUpdate(MapValueUpdate(StringFieldValue("baz"),
                                      ArithmeticValueUpdate(ArithmeticValueUpdate::Add, 0))));

    fixture.applyUpdateToDocument();

    auto ws(fixture.doc.getAs<WeightedSetFieldValue>(fixture.field));
    ASSERT_EQ(size_t(1), ws->size());
    EXPECT_TRUE(ws->contains(StringFieldValue("foo")));
    EXPECT_FALSE(ws->contains(StringFieldValue("baz")));
}

TEST(DocumentUpdateTest, testReadSerializedFile)
{
    // Reads a file serialized from java
    const std::string file_name = "data/crossplatform-java-cpp-doctypes.cfg";
    DocumentTypeRepo repo(readDocumenttypesConfig(file_name));

    int fd = open("data/serializeupdatejava.dat" , O_RDONLY);

    int len = lseek(fd,0,SEEK_END);
    ByteBuffer buf(len);
    lseek(fd,0,SEEK_SET);
    if (read(fd, buf.getBuffer(), len) != len) {
    	throw vespalib::Exception("read failed");
    }
    close(fd);

    nbostream is(buf.getBufferAtPos(), buf.getRemaining());
    DocumentUpdate::UP updp(DocumentUpdate::create42(repo, is));
    DocumentUpdate& upd(*updp);

    const DocumentType *type = repo.getDocumentType("serializetest");
    EXPECT_EQ(DocumentId(DocIdString("update", "test")), upd.getId());
    EXPECT_EQ(*type, upd.getType());

    // Verify assign value update.
    FieldUpdate serField = upd.getUpdates()[1];
    EXPECT_EQ(serField.getField().getId(), type->getField("intfield").getId());

    const ValueUpdate* serValue = &serField[0];
    ASSERT_EQ(serValue->getType(), ValueUpdate::Assign);

    const AssignValueUpdate* assign(static_cast<const AssignValueUpdate*>(serValue));
    EXPECT_EQ(IntFieldValue(4), static_cast<const IntFieldValue&>(assign->getValue()));

    // Verify clear field update.
    serField = upd.getUpdates()[2];
    EXPECT_EQ(serField.getField().getId(), type->getField("floatfield").getId());

    serValue = &serField[0];
    EXPECT_EQ(serValue->getType(), ValueUpdate::Clear);
    EXPECT_TRUE(serValue->inherits(ClearValueUpdate::classId));

    // Verify add value update.
    serField = upd.getUpdates()[0];
    EXPECT_EQ(serField.getField().getId(), type->getField("arrayoffloatfield").getId());

    serValue = &serField[0];
    ASSERT_EQ(serValue->getType(), ValueUpdate::Add);

    const AddValueUpdate* add = static_cast<const AddValueUpdate*>(serValue);
    const FieldValue* value = &add->getValue();
    EXPECT_TRUE(value->inherits(FloatFieldValue::classId));
    EXPECT_FLOAT_EQ(value->getAsFloat(), 5.00f);

    serValue = &serField[1];
    ASSERT_EQ(serValue->getType(), ValueUpdate::Add);

    add = static_cast<const AddValueUpdate*>(serValue);
    value = &add->getValue();
    EXPECT_TRUE(value->inherits(FloatFieldValue::classId));
    EXPECT_FLOAT_EQ(value->getAsFloat(), 4.23f);

    serValue = &serField[2];
    ASSERT_EQ(serValue->getType(), ValueUpdate::Add);

    add = static_cast<const AddValueUpdate*>(serValue);
    value = &add->getValue();
    EXPECT_TRUE(value->inherits(FloatFieldValue::classId));
    EXPECT_FLOAT_EQ(value->getAsFloat(), -1.00f);

}

TEST(DocumentUpdateTest, testGenerateSerializedFile)
{
    // Tests nothing, only generates a file for java test
    const std::string file_name = "data/crossplatform-java-cpp-doctypes.cfg";
    DocumentTypeRepo repo(readDocumenttypesConfig(file_name));

    const DocumentType *type(repo.getDocumentType("serializetest"));
    DocumentUpdate upd(repo, *type, DocumentId(DocIdString("update", "test")));
    upd.addUpdate(FieldUpdate(type->getField("intfield"))
		  .addUpdate(AssignValueUpdate(IntFieldValue(4))));
    upd.addUpdate(FieldUpdate(type->getField("floatfield"))
		  .addUpdate(AssignValueUpdate(FloatFieldValue(1.00f))));
    upd.addUpdate(FieldUpdate(type->getField("arrayoffloatfield"))
		  .addUpdate(AddValueUpdate(FloatFieldValue(5.00f)))
		  .addUpdate(AddValueUpdate(FloatFieldValue(4.23f)))
		  .addUpdate(AddValueUpdate(FloatFieldValue(-1.00f))));
    upd.addUpdate(FieldUpdate(type->getField("intfield"))
          .addUpdate(ArithmeticValueUpdate(ArithmeticValueUpdate::Add, 3)));
    upd.addUpdate(FieldUpdate(type->getField("wsfield"))
          .addUpdate(MapValueUpdate(StringFieldValue("foo"),
                        ArithmeticValueUpdate(ArithmeticValueUpdate::Add, 2)))
          .addUpdate(MapValueUpdate(StringFieldValue("foo"),
                        ArithmeticValueUpdate(ArithmeticValueUpdate::Mul, 2))));
    ByteBuffer::UP buf(serialize42(upd));

    int fd = open("data/serializeupdatecpp.dat", O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (write(fd, buf->getBuffer(), buf->getPos()) != (ssize_t)buf->getPos()) {
	    throw vespalib::Exception("read failed");
    }
    close(fd);
}


TEST(DocumentUpdateTest, testSetBadFieldTypes)
{
    // Create a test document
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    EXPECT_EQ((document::FieldValue*)NULL, doc->getValue(doc->getField("headerval")).get());

    // Assign a float value to an int field.
    DocumentUpdate update(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    ASSERT_THROW(
        update.addUpdate(FieldUpdate(doc->getField("headerval"))
                 .addUpdate(AssignValueUpdate(FloatFieldValue(4.00f)))),
        std::exception) << "Expected exception when adding a float to an int field.";

    update.applyTo(*doc);

    // Verify that the field is NOT set in the document.
    EXPECT_EQ((document::FieldValue*)NULL,
			 doc->getValue(doc->getField("headerval")).get());
}

TEST(DocumentUpdateTest, testUpdateApplyNoParams)
{
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    EXPECT_EQ((document::FieldValue*)NULL, doc->getValue(doc->getField("tags")).get());

    DocumentUpdate update(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    update.addUpdate(FieldUpdate(doc->getField("tags")).addUpdate(AssignValueUpdate()));

    update.applyTo(*doc);

    // Verify that the field was cleared in the document.
    EXPECT_FALSE(doc->hasValue(doc->getField("tags")));
}

TEST(DocumentUpdateTest, testUpdateApplyNoArrayValues)
{
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    const Field &field(doc->getType().getField("tags"));
    EXPECT_EQ((document::FieldValue*) 0, doc->getValue(field).get());

    // Assign array field with no array values = empty array
    DocumentUpdate update(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    update.addUpdate(FieldUpdate(field)
                     .addUpdate(AssignValueUpdate(ArrayFieldValue(field.getDataType()))));

    update.applyTo(*doc);

    // Verify that the field was set in the document
    std::unique_ptr<ArrayFieldValue> fval(doc->getAs<ArrayFieldValue>(field));
    ASSERT_TRUE(fval.get());
    EXPECT_EQ((size_t) 0, fval->size());
}

TEST(DocumentUpdateTest, testUpdateArrayEmptyParamValue)
{
    // Create a test document.
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    const Field &field(doc->getType().getField("tags"));
    EXPECT_EQ((document::FieldValue*) 0, doc->getValue(field).get());

    // Assign array field with no array values = empty array.
    DocumentUpdate update(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    update.addUpdate(FieldUpdate(field).addUpdate(AssignValueUpdate(ArrayFieldValue(field.getDataType()))));
    update.applyTo(*doc);

    // Verify that the field was set in the document.
    std::unique_ptr<ArrayFieldValue> fval1(doc->getAs<ArrayFieldValue>(field));
    ASSERT_TRUE(fval1.get());
    EXPECT_EQ((size_t) 0, fval1->size());

    // Remove array field.
    DocumentUpdate update2(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    update2.addUpdate(FieldUpdate(field).addUpdate(ClearValueUpdate()));
    update2.applyTo(*doc);

    // Verify that the field was cleared in the document.
    std::unique_ptr<ArrayFieldValue> fval2(doc->getAs<ArrayFieldValue>(field));
    EXPECT_FALSE(fval2);
}

TEST(DocumentUpdateTest, testUpdateWeightedSetEmptyParamValue)
{
    // Create a test document
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    const Field &field(doc->getType().getField("stringweightedset"));
    EXPECT_EQ((document::FieldValue*) 0, doc->getValue(field).get());

    // Assign weighted set with no items = empty set.
    DocumentUpdate update(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    update.addUpdate(FieldUpdate(field).addUpdate(AssignValueUpdate(WeightedSetFieldValue(field.getDataType()))));
    update.applyTo(*doc);

    // Verify that the field was set in the document.
    auto fval1(doc->getAs<WeightedSetFieldValue>(field));
    ASSERT_TRUE(fval1.get());
    EXPECT_EQ((size_t) 0, fval1->size());

    // Remove weighted set field.
    DocumentUpdate update2(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    update2.addUpdate(FieldUpdate(field).addUpdate(ClearValueUpdate()));
    update2.applyTo(*doc);

    // Verify that the field was cleared in the document.
    auto fval2(doc->getAs<WeightedSetFieldValue>(field));
    EXPECT_FALSE(fval2);
}

TEST(DocumentUpdateTest, testUpdateArrayWrongSubtype)
{
    // Create a test document
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    const Field &field(doc->getType().getField("tags"));
    EXPECT_EQ((document::FieldValue*) 0, doc->getValue(field).get());

    // Assign int values to string array.
    DocumentUpdate update(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    ASSERT_THROW(
        update.addUpdate(FieldUpdate(field)
                 .addUpdate(AddValueUpdate(IntFieldValue(123)))
                 .addUpdate(AddValueUpdate(IntFieldValue(456)))),
        std::exception) << "Expected exception when adding wrong type.";

    // Apply update
    update.applyTo(*doc);

    // Verify that the field was NOT set in the document
    FieldValue::UP fval(doc->getValue(field));
    EXPECT_EQ((document::FieldValue*) 0, fval.get());
}

TEST(DocumentUpdateTest, testUpdateWeightedSetWrongSubtype)
{
    // Create a test document
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    const Field &field(doc->getType().getField("stringweightedset"));
    EXPECT_EQ((document::FieldValue*) 0, doc->getValue(field).get());

    // Assign int values to string array.
    DocumentUpdate update(docMan.getTypeRepo(), *doc->getDataType(), doc->getId());
    ASSERT_THROW(
        update.addUpdate(FieldUpdate(field)
                 .addUpdate(AddValueUpdate(IntFieldValue(123)).setWeight(1000))
                 .addUpdate(AddValueUpdate(IntFieldValue(456)).setWeight(2000))),
        std::exception) << "Expected exception when adding wrong type.";

    // Apply update
    update.applyTo(*doc);

    // Verify that the field was NOT set in the document
    FieldValue::UP fval(doc->getValue(field));
    EXPECT_EQ((document::FieldValue*) 0, fval.get());
}

TEST(DocumentUpdateTest, testMapValueUpdate)
{
    // Create a test document
    TestDocMan docMan;
    Document::UP doc(docMan.createDocument());
    const Field &field1 = doc->getField("stringweightedset");
    const Field &field2 = doc->getField("stringweightedset2");
    WeightedSetFieldValue wsval1(field1.getDataType());
    WeightedSetFieldValue wsval2(field2.getDataType());
    doc->setValue(field1, wsval1);
    doc->setValue(field2, wsval2);

    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field1)
                   .addUpdate(MapValueUpdate(StringFieldValue("banana"),
                                             ArithmeticValueUpdate(ArithmeticValueUpdate::Add, 1.0))))
        .applyTo(*doc);
    std::unique_ptr<WeightedSetFieldValue> fv1 =
        doc->getAs<WeightedSetFieldValue>(field1);
    EXPECT_EQ(0, fv1->size());

    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field2)
                   .addUpdate(MapValueUpdate(StringFieldValue("banana"),
                                             ArithmeticValueUpdate(ArithmeticValueUpdate::Add, 1.0))))
        .applyTo(*doc);
    auto fv2 = doc->getAs<WeightedSetFieldValue>(field2);
    EXPECT_EQ(1, fv2->size());

    EXPECT_EQ(fv1->find(StringFieldValue("apple")), fv1->end());
    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field1).addUpdate(ClearValueUpdate()))
        .applyTo(*doc);

    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field1).addUpdate(AddValueUpdate(StringFieldValue("apple")).setWeight(1)))
        .applyTo(*doc);

    auto fval3(doc->getAs<WeightedSetFieldValue>(field1));
    EXPECT_NE(fval3->find(StringFieldValue("apple")), fval3->end());
    EXPECT_EQ(1, fval3->get(StringFieldValue("apple")));

    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field2).addUpdate(AddValueUpdate(StringFieldValue("apple")).setWeight(1)))
        .applyTo(*doc);

    auto fval3b(doc->getAs<WeightedSetFieldValue>(field2));
    EXPECT_NE(fval3b->find(StringFieldValue("apple")), fval3b->end());
    EXPECT_EQ(1, fval3b->get(StringFieldValue("apple")));

    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field1)
                   .addUpdate(MapValueUpdate(StringFieldValue("apple"),
                                             ArithmeticValueUpdate(ArithmeticValueUpdate::Sub, 1.0))))
        .applyTo(*doc);

    auto fv3 = doc->getAs<WeightedSetFieldValue>(field1);
    EXPECT_NE(fv3->find(StringFieldValue("apple")), fv3->end());
    EXPECT_EQ(0, fv3->get(StringFieldValue("apple")));

    DocumentUpdate(docMan.getTypeRepo(), *doc->getDataType(), doc->getId())
        .addUpdate(FieldUpdate(field2)
                   .addUpdate(MapValueUpdate(StringFieldValue("apple"),
                                             ArithmeticValueUpdate(ArithmeticValueUpdate::Sub, 1.0))))
        .applyTo(*doc);

    auto fv4 = doc->getAs<WeightedSetFieldValue>(field2);
    EXPECT_EQ(fv4->find(StringFieldValue("apple")), fv4->end());
}

std::unique_ptr<Tensor>
makeTensor(const TensorSpec &spec)
{
    auto result = DefaultTensorEngine::ref().from_spec(spec);
    return std::unique_ptr<Tensor>(dynamic_cast<Tensor*>(result.release()));
}

std::unique_ptr<TensorFieldValue>
makeTensorFieldValue(const TensorSpec &spec, const TensorDataType &dataType)
{
    auto tensor = makeTensor(spec);
    auto result = std::make_unique<TensorFieldValue>(dataType);
    *result = std::move(tensor);
    return result;
}

const Tensor &asTensor(const FieldValue &fieldValue) {
    auto &tensorFieldValue = dynamic_cast<const TensorFieldValue &>(fieldValue);
    auto &tensor = tensorFieldValue.getAsTensorPtr();
    assert(tensor);
    return *tensor;
}

struct TensorUpdateFixture {
    TestDocMan docMan;
    Document::UP emptyDoc;
    Document updatedDoc;
    vespalib::string fieldName;
    const TensorDataType &tensorDataType;
    vespalib::string tensorType;

    const TensorDataType &extractTensorDataType() {
        const auto &dataType = emptyDoc->getField(fieldName).getDataType();
        return dynamic_cast<const TensorDataType &>(dataType);
    }

    const Field &getNonTensorField() {
        return emptyDoc->getField("title");
    }

    TensorUpdateFixture(const vespalib::string &fieldName_ = "sparse_tensor")
        : docMan(),
          emptyDoc(docMan.createDocument()),
          updatedDoc(*emptyDoc),
          fieldName(fieldName_),
          tensorDataType(extractTensorDataType()),
          tensorType(tensorDataType.getTensorType().to_spec())
    {
        EXPECT_FALSE(emptyDoc->getValue(fieldName));
    }
    ~TensorUpdateFixture() {}

    TensorSpec spec() {
        return TensorSpec(tensorType);
    }

    FieldValue::UP getTensor() {
        return updatedDoc.getValue(fieldName);
    }

    void setTensor(const TensorFieldValue &tensorValue) {
        updatedDoc.setValue(updatedDoc.getField(fieldName), tensorValue);
        assertDocumentUpdated();
    }

    void setTensor(const TensorSpec &spec) {
        setTensor(*makeTensor(spec));
    }

    std::unique_ptr<TensorFieldValue> makeTensor(const TensorSpec &spec) {
        return makeTensorFieldValue(spec, tensorDataType);
    }

    std::unique_ptr<TensorFieldValue> makeBaselineTensor() {
        return makeTensor(spec().add({{"x", "a"}}, 2)
                                  .add({{"x", "b"}}, 3));
    }

    void applyUpdate(const ValueUpdate &update) {
        DocumentUpdate docUpdate(docMan.getTypeRepo(), *emptyDoc->getDataType(), emptyDoc->getId());
        docUpdate.addUpdate(FieldUpdate(docUpdate.getType().getField(fieldName)).addUpdate(update));
        docUpdate.applyTo(updatedDoc);
    }

    void assertDocumentUpdated() {
        EXPECT_NE(*emptyDoc, updatedDoc);
    }

    void assertDocumentNotUpdated() {
        EXPECT_EQ(*emptyDoc, updatedDoc);
    }

    void assertTensor(const TensorFieldValue &expTensorValue) {
        auto actTensorValue = getTensor();
        ASSERT_TRUE(actTensorValue);
        EXPECT_EQ(*actTensorValue, expTensorValue);
        auto &actTensor = asTensor(*actTensorValue);
        auto &expTensor = asTensor(expTensorValue);
        EXPECT_EQ(actTensor, expTensor);
    }

    void assertTensor(const TensorSpec &expSpec) {
        auto expTensor = makeTensor(expSpec);
        assertTensor(*expTensor);
    }

    void assertApplyUpdate(const TensorSpec &initialTensor,
                           const ValueUpdate &update,
                           const TensorSpec &expTensor) {
        setTensor(initialTensor);
        applyUpdate(update);
        assertDocumentUpdated();
        assertTensor(expTensor);
    }

    template <typename ValueUpdateType>
    void assertRoundtripSerialize(const ValueUpdateType &valueUpdate) {
        testRoundtripSerialize(valueUpdate, tensorDataType);
    }

    void assertThrowOnNonTensorField(const ValueUpdate &update) {
        ASSERT_THROW(update.checkCompatibility(getNonTensorField()),
                     vespalib::IllegalArgumentException);
        StringFieldValue value("my value");
        ASSERT_THROW(update.applyTo(value),
                     vespalib::IllegalStateException);
    }

};

TEST(DocumentUpdateTest, tensor_assign_update_can_be_applied)
{
    TensorUpdateFixture f;
    auto newTensor = f.makeBaselineTensor();
    f.applyUpdate(AssignValueUpdate(*newTensor));
    f.assertDocumentUpdated();
    f.assertTensor(*newTensor);
}

TEST(DocumentUpdateTest, tensor_clear_update_can_be_applied)
{
    TensorUpdateFixture f;
    f.setTensor(*f.makeBaselineTensor());
    f.applyUpdate(ClearValueUpdate());
    f.assertDocumentNotUpdated();
    EXPECT_FALSE(f.getTensor());
}

TEST(DocumentUpdateTest, tensor_add_update_can_be_applied)
{
    TensorUpdateFixture f;
    f.assertApplyUpdate(f.spec().add({{"x", "a"}}, 2)
                                .add({{"x", "b"}}, 3),

                        TensorAddUpdate(f.makeTensor(f.spec().add({{"x", "b"}}, 5)
                                                             .add({{"x", "c"}}, 7))),

                        f.spec().add({{"x", "a"}}, 2)
                                .add({{"x", "b"}}, 5)
                                .add({{"x", "c"}}, 7));
}

TEST(DocumentUpdateTest, tensor_remove_update_can_be_applied)
{
    TensorUpdateFixture f;
    f.assertApplyUpdate(f.spec().add({{"x", "a"}}, 2)
                                .add({{"x", "b"}}, 3),

                        TensorRemoveUpdate(f.makeTensor(f.spec().add({{"x", "b"}}, 1))),

                        f.spec().add({{"x", "a"}}, 2));
}

TEST(DocumentUpdateTest, tensor_modify_update_can_be_applied)
{
    TensorUpdateFixture f;
    auto baseLine = f.spec().add({{"x", "a"}}, 2)
                            .add({{"x", "b"}}, 3);

    f.assertApplyUpdate(baseLine,
                        TensorModifyUpdate(TensorModifyUpdate::Operation::REPLACE,
                                           f.makeTensor(f.spec().add({{"x", "b"}}, 5)
                                                                .add({{"x", "c"}}, 7))),
                        f.spec().add({{"x", "a"}}, 2)
                                .add({{"x", "b"}}, 5));

    f.assertApplyUpdate(baseLine,
                        TensorModifyUpdate(TensorModifyUpdate::Operation::ADD,
                                           f.makeTensor(f.spec().add({{"x", "b"}}, 5))),
                        f.spec().add({{"x", "a"}}, 2)
                                .add({{"x", "b"}}, 8));

    f.assertApplyUpdate(baseLine,
                        TensorModifyUpdate(TensorModifyUpdate::Operation::MULTIPLY,
                                           f.makeTensor(f.spec().add({{"x", "b"}}, 5))),
                        f.spec().add({{"x", "a"}}, 2)
                                .add({{"x", "b"}}, 15));
}

TEST(DocumentUpdateTest, tensor_assign_update_can_be_roundtrip_serialized)
{
    TensorUpdateFixture f;
    f.assertRoundtripSerialize(AssignValueUpdate(*f.makeBaselineTensor()));
}

TEST(DocumentUpdateTest, tensor_add_update_can_be_roundtrip_serialized)
{
    TensorUpdateFixture f;
    f.assertRoundtripSerialize(TensorAddUpdate(f.makeBaselineTensor()));
}

TEST(DocumentUpdateTest, tensor_remove_update_can_be_roundtrip_serialized)
{
    TensorUpdateFixture f;
    f.assertRoundtripSerialize(TensorRemoveUpdate(f.makeBaselineTensor()));
}

TEST(DocumentUpdateTest, tensor_modify_update_can_be_roundtrip_serialized)
{
    TensorUpdateFixture f;
    f.assertRoundtripSerialize(TensorModifyUpdate(TensorModifyUpdate::Operation::REPLACE, f.makeBaselineTensor()));
    f.assertRoundtripSerialize(TensorModifyUpdate(TensorModifyUpdate::Operation::ADD, f.makeBaselineTensor()));
    f.assertRoundtripSerialize(TensorModifyUpdate(TensorModifyUpdate::Operation::MULTIPLY, f.makeBaselineTensor()));
}

TEST(DocumentUpdateTest, tensor_add_update_throws_on_non_tensor_field)
{
    TensorUpdateFixture f;
    f.assertThrowOnNonTensorField(TensorAddUpdate(f.makeBaselineTensor()));
}

TEST(DocumentUpdateTest, tensor_remove_update_throws_on_non_tensor_field)
{
    TensorUpdateFixture f;
    f.assertThrowOnNonTensorField(TensorRemoveUpdate(f.makeBaselineTensor()));
}

TEST(DocumentUpdateTest, tensor_modify_update_throws_on_non_tensor_field)
{
    TensorUpdateFixture f;
    f.assertThrowOnNonTensorField(TensorModifyUpdate(TensorModifyUpdate::Operation::REPLACE, f.makeBaselineTensor()));
}


void
assertDocumentUpdateFlag(bool createIfNonExistent, int value)
{
    DocumentUpdateFlags f1;
    f1.setCreateIfNonExistent(createIfNonExistent);
    EXPECT_EQ(createIfNonExistent, f1.getCreateIfNonExistent());
    int combined = f1.injectInto(value);
    std::cout << "createIfNonExistent=" << createIfNonExistent << ", value=" << value << ", combined=" << combined << std::endl;

    DocumentUpdateFlags f2 = DocumentUpdateFlags::extractFlags(combined);
    int extractedValue = DocumentUpdateFlags::extractValue(combined);
    EXPECT_EQ(createIfNonExistent, f2.getCreateIfNonExistent());
    EXPECT_EQ(value, extractedValue);
}

TEST(DocumentUpdateTest, testThatDocumentUpdateFlagsIsWorking)
{
    { // create-if-non-existent = true
        assertDocumentUpdateFlag(true, 0);
        assertDocumentUpdateFlag(true, 1);
        assertDocumentUpdateFlag(true, 2);
        assertDocumentUpdateFlag(true, 9999);
        assertDocumentUpdateFlag(true, 0xFFFFFFE);
        assertDocumentUpdateFlag(true, 0xFFFFFFF);
    }
    { // create-if-non-existent = false
        assertDocumentUpdateFlag(false, 0);
        assertDocumentUpdateFlag(false, 1);
        assertDocumentUpdateFlag(false, 2);
        assertDocumentUpdateFlag(false, 9999);
        assertDocumentUpdateFlag(false, 0xFFFFFFE);
        assertDocumentUpdateFlag(false, 0xFFFFFFF);
    }
}

struct CreateIfNonExistentFixture
{
    TestDocMan docMan;
    Document::UP document;
    DocumentUpdate::UP update;
    ~CreateIfNonExistentFixture();
    CreateIfNonExistentFixture();
};

CreateIfNonExistentFixture::~CreateIfNonExistentFixture() = default;
CreateIfNonExistentFixture::CreateIfNonExistentFixture()
    : docMan(),
      document(docMan.createDocument()),
      update(new DocumentUpdate(docMan.getTypeRepo(), *document->getDataType(), document->getId()))
{
    update->addUpdate(FieldUpdate(document->getField("headerval"))
                              .addUpdate(AssignValueUpdate(IntFieldValue(1))));
    update->setCreateIfNonExistent(true);
}

TEST(DocumentUpdateTest, testThatCreateIfNonExistentFlagIsSerialized50AndDeserialized50)
{
    CreateIfNonExistentFixture f;

    ByteBuffer::UP buf(serializeHEAD(*f.update));
    buf->flip();

    DocumentUpdate::UP deserialized = DocumentUpdate::createHEAD(f.docMan.getTypeRepo(), *buf);
    EXPECT_EQ(*f.update, *deserialized);
    EXPECT_TRUE(deserialized->getCreateIfNonExistent());
}

TEST(DocumentUpdateTest, testThatCreateIfNonExistentFlagIsSerializedAndDeserialized)
{
    CreateIfNonExistentFixture f;

    ByteBuffer::UP buf(serialize42(*f.update));
    buf->flip();

    nbostream is(buf->getBufferAtPos(), buf->getRemaining());
    auto deserialized = DocumentUpdate::create42(f.docMan.getTypeRepo(), is);
    EXPECT_EQ(*f.update, *deserialized);
    EXPECT_TRUE(deserialized->getCreateIfNonExistent());
}

struct ArrayUpdateFixture {
    TestDocMan doc_man;
    std::unique_ptr<Document> doc;
    const Field& array_field;
    std::unique_ptr<DocumentUpdate> update;

    ArrayUpdateFixture();
    ~ArrayUpdateFixture();
};

ArrayUpdateFixture::ArrayUpdateFixture()
    : doc_man(),
      doc(doc_man.createDocument()),
      array_field(doc->getType().getField("tags")) // of type array<string>
{
    update = std::make_unique<DocumentUpdate>(doc_man.getTypeRepo(), *doc->getDataType(), doc->getId());
    update->addUpdate(FieldUpdate(array_field)
                              .addUpdate(MapValueUpdate(IntFieldValue(1),
                                                        AssignValueUpdate(StringFieldValue("bar")))));
}
ArrayUpdateFixture::~ArrayUpdateFixture() = default;

TEST(DocumentUpdateTest, array_element_update_can_be_roundtrip_serialized)
{
    ArrayUpdateFixture f;

    auto buffer = serializeHEAD(*f.update);
    buffer->flip();

    auto deserialized = DocumentUpdate::createHEAD(f.doc_man.getTypeRepo(), *buffer);
    EXPECT_EQ(*f.update, *deserialized);
}

TEST(DocumentUpdateTest, array_element_update_applies_to_specified_element)
{
    ArrayUpdateFixture f;

    ArrayFieldValue array_value(f.array_field.getDataType());
    array_value.add("foo");
    array_value.add("baz");
    array_value.add("blarg");
    f.doc->setValue(f.array_field, array_value);

    f.update->applyTo(*f.doc);

    auto result_array = f.doc->getAs<ArrayFieldValue>(f.array_field);
    ASSERT_EQ(size_t(3), result_array->size());
    EXPECT_EQ(vespalib::string("foo"), (*result_array)[0].getAsString());
    EXPECT_EQ(vespalib::string("bar"), (*result_array)[1].getAsString());
    EXPECT_EQ(vespalib::string("blarg"), (*result_array)[2].getAsString());
}

TEST(DocumentUpdateTest, array_element_update_for_invalid_index_is_ignored)
{
    ArrayUpdateFixture f;

    ArrayFieldValue array_value(f.array_field.getDataType());
    array_value.add("jerry");
    f.doc->setValue(f.array_field, array_value);

    f.update->applyTo(*f.doc); // MapValueUpdate for index 1, which does not exist

    auto result_array = f.doc->getAs<ArrayFieldValue>(f.array_field);
    EXPECT_EQ(array_value, *result_array);
}

}  // namespace document
