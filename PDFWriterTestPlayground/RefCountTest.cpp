#include "RefCountTest.h"
#include "RefCountPtr.h"
#include "RefCountObject.h"

#include <iostream>

using namespace std;

RefCountTest::RefCountTest(void)
{
}

RefCountTest::~RefCountTest(void)
{
}

// this will serve as our sample class
class MyClass : public RefCountObject
{
public:
	static int TotalObjectsCount;

	MyClass(int inID){mID = inID; ++TotalObjectsCount;}
	virtual ~MyClass(){--TotalObjectsCount;}
	
	int GetID(){return mID;}

private:
	int mID;
};

int MyClass::TotalObjectsCount = 0;

EStatusCode RefCountTest::Run()
{
	EStatusCode status = eSuccess;

	if(MyClass::TotalObjectsCount != 0)
	{
		wcout<<"Total objects count is supposed to be 0 at the beginning, but it's "<<MyClass::TotalObjectsCount<<"\n";
		status = eFailure;
	}

	// simple 1 ref test
	{
		RefCountPtr<MyClass> firstPtr(new MyClass(1));
		if(MyClass::TotalObjectsCount != 1)
		{
			wcout<<"simple 1 ref test failed, TotalObjectCount (should be 1) = "<<MyClass::TotalObjectsCount<<"\n";
			status = eFailure;
		}
		if(firstPtr->GetID() != 1)
		{
			wcout<<"simple 1 ref test failed, wrond object id\n";
			status = eFailure;
		}
	}
	if(MyClass::TotalObjectsCount != 0)
	{
		wcout<<"simple 1 ref test failed, TotalObjectCount (should be 0) = "<<MyClass::TotalObjectsCount<<"\n";
		status = eFailure;
	}

	// 2 refs to direct object
	{
		MyClass* aClass = new MyClass(2);

		if(MyClass::TotalObjectsCount != 1)
		{
			wcout<<"Total objects count is supposed to be 1 after creating object 2, but it's "<<MyClass::TotalObjectsCount<<"\n";
			status = eFailure;
		}
		RefCountPtr<MyClass> firstPtr(aClass);
		if(firstPtr->GetID() != 2)
		{
			wcout<<"2 ref test failed, wrond object id for pointer 1\n";
			status = eFailure;
		}

		aClass->AddRef();
		RefCountPtr<MyClass> secondPtr(aClass);
		if(secondPtr->GetID() != 2)
		{
			wcout<<"2 ref test failed, wrond object id for pointer 2\n";
			status = eFailure;
		}
		if(MyClass::TotalObjectsCount != 1)
		{
			wcout<<"Total objects count is supposed to be 1 after another pointer for object 2, but it's "<<MyClass::TotalObjectsCount<<"\n";
			status = eFailure;
		}
	}
	if(MyClass::TotalObjectsCount != 0)
	{
		wcout<<"2 ref test failed, TotalObjectCount = "<<MyClass::TotalObjectsCount<<"\n";
		status = eFailure;
	}

	// assignment scenarios
	{
		RefCountPtr<MyClass> firstPtr(new MyClass(3));

		if(MyClass::TotalObjectsCount != 1)
		{
			wcout<<"Total objects count is supposed to be 1 after creating object 3, but it's "<<MyClass::TotalObjectsCount<<"\n";
			status = eFailure;
		}
		if(firstPtr->GetID() != 3)
		{
			wcout<<"assignment test failed, wrond object id for pointer 1\n";
			status = eFailure;
		}

		{
			RefCountPtr<MyClass> secondPtr;

			secondPtr = firstPtr;
			if(secondPtr->GetID() != 3)
			{
				wcout<<"assignment test failed, wrond object id for pointer 2\n";
				status = eFailure;
			}
		}
		if(MyClass::TotalObjectsCount != 1)
		{
			wcout<<"Total objects count is supposed to be 1 after smart pointer assignment object 3, but it's "<<MyClass::TotalObjectsCount<<"\n";
			status = eFailure;
		}

		{
			RefCountPtr<MyClass> thirdPtr;

			thirdPtr = firstPtr.GetPtr();
			if(thirdPtr->GetID() != 3)
			{
				wcout<<"assignment test failed, wrond object id for pointer 2\n";
				status = eFailure;
			}
		}
		if(MyClass::TotalObjectsCount != 1)
		{
			wcout<<"Total objects count is supposed to be 1 after pointer assignment object 3, but it's "<<MyClass::TotalObjectsCount<<"\n";
			status = eFailure;
		}
	}
	if(MyClass::TotalObjectsCount != 0)
	{
		wcout<<"assignment test failed, TotalObjectCount = "<<MyClass::TotalObjectsCount<<"\n";
		status = eFailure;
	}

	// pointer equality
	{
		MyClass* anObject = new MyClass(4);
		
		RefCountPtr<MyClass> firstPtr(anObject);
		RefCountPtr<MyClass> secondPtr = firstPtr;

		if(firstPtr != secondPtr)
		{
			wcout<<"smart pointers equality failed (not equal)\n";
			status = eFailure;
		}

		if(!(firstPtr == secondPtr))
		{
			wcout<<"smart pointers equality failed (equal)\n";
			status = eFailure;
		}

		if(firstPtr != anObject)
		{
			wcout<<"smart pointer to pointer equality failed (not equal)\n";
			status = eFailure;
		}

		if(!(firstPtr == anObject))
		{
			wcout<<"smart pointer to pointer equality failed (equal)\n";
			status = eFailure;
		}
	}
	if(MyClass::TotalObjectsCount != 0)
	{
		wcout<<"Pointer equality, TotalObjectCount = "<<MyClass::TotalObjectsCount<<"\n";
		status = eFailure;
	}


	// boolean test
	{
		RefCountPtr<MyClass> aPtr(new MyClass(5));

		if(!aPtr)
		{
			wcout<<"Problem, should not be false!\n";
			status = eFailure;
		}		
		
		RefCountPtr<MyClass> aNother;
		if(!(!aNother))
		{
			wcout<<"Problem, should be false!\n";
			status = eFailure;			
		}
	}

	return status;
}


ADD_CETEGORIZED_TEST(RefCountTest,"PDFEmbedding")