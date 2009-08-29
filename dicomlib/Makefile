#I'm very tempted to move to scons....
#ok, i did it!  This file now officially obsolete, ha ha ha!
all:
	echo "this file is obsolete: scons is the makesystem to rule all makesystems!"

CPP = g++ -c -Wall  -I .. #-ggdb


libdicom.a:aaac.o aarj.o aarq.o AssociationRejection.o Buffer.o Cdimse.o ClientConnection.o DataDictionary.o Decoder.o Dumper.o Encoder.o Exceptions.o\
	File.o FileMetaInformation.o\
	Ndimse.o pdata.o Profiling.o QueryRetrieveLevel.o Server.o ServiceBase.o ThreadSpecificServer.o TransferSyntax.o UID.o Utility.o ValueToStream.o
	ar -rcv libdicom.a *.o

aaac.o:aaac.cpp aaac.hpp
	$(CPP)  aaac.cpp
AssociationRejection.o:AssociationRejection.cpp AssociationRejection.hpp
	$(CPP)  AssociationRejection.cpp
aarj.o:aarj.cpp aarj.hpp
	$(CPP)  aarj.cpp
Buffer.o:Buffer.cpp Buffer.hpp
	$(CPP)  Buffer.cpp
aarq.o:aarq.cpp aarq.hpp
	$(CPP)  aarq.cpp
Cdimse.o:Cdimse.cpp Cdimse.hpp
	$(CPP)  Cdimse.cpp
ClientConnection.o:ClientConnection.cpp ClientConnection.hpp
	$(CPP)  ClientConnection.cpp
DataDictionary.o:DataDictionary.cpp DataDictionary.hpp
	$(CPP)  DataDictionary.cpp
Decoder.o:Decoder.cpp Decoder.hpp
	$(CPP)  Decoder.cpp
Dumper.o:Dumper.cpp Dumper.hpp
	$(CPP)  Dumper.cpp
Encoder.o:Encoder.cpp Encoder.hpp
	$(CPP)  Encoder.cpp
Exceptions.o:Exceptions.cpp Exceptions.hpp
	$(CPP)  Exceptions.cpp
File.o:File.cpp File.hpp
	$(CPP) File.cpp
FileMetaInformation.o:FileMetaInformation.cpp FileMetaInformation.hpp
	$(CPP) FileMetaInformation.cpp
Ndimse.o:Ndimse.cpp Ndimse.hpp
	$(CPP)  Ndimse.cpp

pdata.o:pdata.cpp pdata.hpp
	$(CPP)  pdata.cpp
Profiling.o:Profiling.cpp Profiling.hpp
	$(CPP)  Profiling.cpp
QueryRetrieveLevel.o:QueryRetrieveLevel.cpp QueryRetrieveLevel.hpp
	$(CPP)  QueryRetrieveLevel.cpp
Server.o:Server.cpp Server.hpp aarq.hpp
	$(CPP)  Server.cpp
ServiceBase.o:ServiceBase.cpp ServiceBase.hpp
	$(CPP)  ServiceBase.cpp
ThreadSpecificServer.o:ThreadSpecificServer.cpp ThreadSpecificServer.hpp
	$(CPP)  ThreadSpecificServer.cpp
TransferSyntax.o:TransferSyntax.cpp TransferSyntax.hpp
	$(CPP)  TransferSyntax.cpp

UID.o:UID.cpp UID.hpp
	$(CPP)  UID.cpp
Utility.o:Utility.cpp Utility.hpp
	$(CPP)  Utility.cpp

ValueToStream.o:ValueToStream.cpp ValueToStream.hpp
	$(CPP) ValueToStream.cpp

UnitTests:UnitTests.cpp
	g++ -g UnitTests.cpp -o UnitTests -I/home/morgan/Code/socket -L. -ldicom

Documentation:
	doxygen

clean:
	rm -f *.o *.so *.a UnitTests
	rm -rf html
