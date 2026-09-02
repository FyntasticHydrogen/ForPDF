# Bölüm 2: Frontend Tasarımı, AST, LLVM IR Yapısı ve Bellek Modeli

## 2.1 Soyut Sözdizim Ağacı (AST) Tasarımı

Ön Yüz (Frontend) mimarisinin kalbinde **Soyut Sözdizim Ağacı (Abstract Syntax Tree - AST)** yer alır. AST, kaynak kodun metinsel halindeki gereksiz karakterleri (parantezler, noktalı virgüller, boşluklar vb.) ayıklayarak kodun mantıksal ve dilbilgisel hiyerarşisini ağaç veri yapısı şeklinde temsil eder. Oyun programlama dilimizde hem ifade (expression) hem de bildirim/deyim (statement) düğümlerine ihtiyacımız vardır.

Modern C++ (C++17/20) standartlarında AST düğümlerini `std::unique_ptr` ve sanal fonksiyonlar (polymorphism) ile temsil ederiz. Her AST düğümü, kendini LLVM IR'a dönüştürme sorumluluğuna (`codegen()`) sahiptir.

```
                  [FunctionAST: "UpdatePlayerPosition"]
                                   |
                         [BlockExprAST / Body]
                                   |
             +---------------------+---------------------+
             |                                           |
    [VarDeclAST: "pos"]                         [BinaryExprAST: "+="]
             |                                           |
    [Type: Vector3D]                    +----------------+----------------+
                                        |                                 |
                                 [VariableExprAST: "pos"]        [BinaryExprAST: "*"]
                                                                          |
                                                                  +-------+-------+
                                                                  |               |
                                                           [VariableAST]   [VariableAST]
                                                               "velocity"      "deltaTime"
```

---

## 2.2 LLVM IR (Intermediate Representation) Derinlemesine İnceleme

LLVM IR, LLVM derleyici altyapısının merkezindeki ortak dildir. Üç farklı biçimde temsil edilebilir:
1. **Hafızadaki (In-Memory) Veri Yapısı:** C++ sınıfları (`llvm::Instruction`, `llvm::Value`, `llvm::BasicBlock`).
2. **Disk Üzerindeki Okunabilir Metin Biçimi (.ll):** İnsanlar tarafından kolayca okunabilen montaj dili benzeri yapı.
3. **Disk Üzerindeki İkili Bitcode Biçimi (.bc):** Derleyicilerin hızlı işleyebilmesi için optimize edilmiş ikili format.

LLVM IR, güçlü bir şekilde tiplendirilmiş (strongly typed), sonsuz sayıda sanal yazmaca (virtual register) sahip ve **Tekli Atama Biçimine (Static Single Assignment - SSA)** dayalı bir yapıdır.

---

## 2.3 Tekli Atama Biçimine (Static Single Assignment - SSA) ve PHI Düğümleri

### SSA Nedir ve Neden Hayatidir?
SSA formunda, her sanal yazmaca veya değişkene **yalnızca bir kez** değer atanabilir. Klasik bir programlama dilinde bir değişkene birden fazla kez değer atanabilir:

```c
int health = 100;
health = health - damage;
health = health + healAmount;
```

Bu kod SSA yapısına dönüştürüldüğünde, her atama yeni bir SSA sürüm değişkeni (register) oluşturur:

```llvm
%health0 = copy i32 100
%health1 = sub i32 %health0, %damage
%health2 = add i32 %health1, %healAmount
```

* **Çalışma Algoritması ve Avantajı:** SSA formunun derleyicilere sağladığı en büyük avantaj, **Veri Akışı Analizini (Data Flow Analysis)** doğrusal hale getirmesidir. Bir yazmacın değerinin nereden geldiği ve nerede değiştiği tartışmasız bir şekilde bellidir. Derleyici, karmaşık gösterici (pointer) takibi yapmadan bağımlılıkları görür.
* **Benzetme:** SSA formunu bir muhasebe defterindeki **Silinemez Günlük Kayıtlara (Immutable Ledger / Blockchain)** benzetebiliriz. Eski bir girdinin üzerini çizip değiştiremezsiniz; her yeni durum için yeni bir satır kaydı açarsınız.

### PHI Düğümleri (`phi` instruction) ve C++ ile Oluşturulması
Dallanma (if-else, döngüler) olan durumlarda bir değişkenin hangi kontrol akışı yolundan geldiği çalışma zamanında belli olur. SSA kuralını bozmadan bu durumu çözmek için **PHI Düğümleri (`phi` instruction)** kullanılır.

```llvm
entry:
  %cmp = icmp sgt i32 %damage, %shield
  br i1 %cmp, label %take_damage, label %blocked

take_damage:
  %hp_after_hit = sub i32 %current_hp, %damage
  br label %merge

blocked:
  %hp_after_block = select i1 true, i32 %current_hp, i32 0
  br label %merge

merge:
  %final_hp = phi i32 [ %hp_after_hit, %take_damage ], [ %hp_after_block, %blocked ]
  ret i32 %final_hp
```

#### PHI Düğümünün C++ API'si İle Tanımlanması:

```cpp
llvm::PHINode* phiNode = builder.CreatePHI(builder.getInt32Ty(), 2, "final_hp");

phiNode->addIncoming(hpAfterHitVal, takeDamageBB);

phiNode->addIncoming(hpAfterBlockVal, blockedBB);
```

#### Adım Adım Açıklama ve Çalışma İlkesi:

1. **`builder.CreatePHI` İle PHI Nesnesinin Başlatılması:**
   * **İlk Parametre (`builder.getInt32Ty()`):** PHI düğümünün temsil edeceği verinin tipidir. Örneğimizde can değeri 32-bit tamsayı (`i32`) olduğu için tamsayı tipi verilir.
   * **İkinci Parametre (`2`):** PHI düğümüne dallanabilecek olası kaynak Basic Block sayısı tahminidir (pre-allocation). `take_damage` ve `blocked` olmak üzere 2 farklı rotamız olduğu için `2` değeri verilmiştir.
   * **Üçüncü Parametre (`"final_hp"`):** Üretilecek olan LLVM IR SSA sanal yazmacının adıdır.
2. **`phiNode->addIncoming` İle Rota ve Değer Eşleştirilmesi:**
   * **`takeDamageBB` Bağlantısı:** İlk `addIncoming` çağrısı, "Eğer yürütme akışı `takeDamageBB` bloğundan `merge` bloğuna geldiysa, `%final_hp` yazmacına `%hp_after_hit` değerini yükle" kuralını tanımlar.
   * **`blockedBB` Bağlantısı:** İkinci `addIncoming` çağrısı ise, "Eğer yürütme akışı `blockedBB` bloğundan geldiyse, `%hp_after_block` değerini yükle" kuralını tanımlar.

<div class="callout callout-warning">
<div class="callout-title">PHI Düğümü Kuralı</div>
LLVM IR içerisinde bir PHI düğümü talimatı (`phi`), bulunacağı Basic Block'un **en başında** (diğer tüm işlem talimatlarından önce) yer almak zorundadır!
</div>

---

## 2.4 Temel Yapı Taşları (Basic Blocks), Dallanmalar ve C++ İle Kontrol Akış Grafiği (CFG) İnşası

Bir **Temel Yapı Taşı (Basic Block)**, içerisine yalnızca tek bir noktadan girilen (ilk talimat) ve yalnızca tek bir noktadan çıkılan (son talimat) talimatlar dizisidir.

Bir Basic Block'un son talimatı mutlaka bir **Terminator Instruction** (Sonlandırıcı Talimat) olmalıdır. Örnek sonlandırıcılar:
* `ret`: Fonksiyondan döner.
* `br`: Başka bir Basic Block'a dallanır (koşullu veya koşulsuz).
* `switch`: Çoklu dallanma yapar.

### C++ Tarafında Koşullu Dallanma (If-Else) ve CFG İnşası Adım Adım:

Aşağıdaki C++ kod parçası, oyuncunun canı 0'dan büyükse oyuncunun yaşamaya devam ettiği (`alive`), aksi halde öldüğü (`dead`) kontrol akış grafiğini (CFG) inşa eder:

```cpp
llvm::BasicBlock* aliveBB = llvm::BasicBlock::Create(*context, "alive", playerFunc);
llvm::BasicBlock* deadBB  = llvm::BasicBlock::Create(*context, "dead", playerFunc);
llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "merge", playerFunc);

llvm::Value* condValue = builder.CreateICmpSGT(healthVal, builder.getInt32(0), "is_alive");

builder.CreateCondBr(condValue, aliveBB, deadBB);

builder.SetInsertPoint(aliveBB);
builder.CreateBr(mergeBB);

builder.SetInsertPoint(deadBB);
builder.CreateBr(mergeBB);

builder.SetInsertPoint(mergeBB);
```

#### Adım Adım Açıklama ve Çalışma İlkesi:

1. **`BasicBlock::Create` İle Akış Bloklarının Tanımlanması:**
   * `aliveBB`, `deadBB` ve `mergeBB` olmak üzere üç adet kontrol bloğu oluşturulur. Parametre olarak geçilen `playerFunc` göstericisi, bu blokların `playerFunc` fonksiyonuna ait olduğunu söyler.
2. **Koşul Karşılaştırması (`CreateICmpSGT`):**
   * `builder.CreateICmpSGT` (Integer Compare Signed Greater Than) metodu, oyuncunun canını temsil eden `healthVal` yazmacı ile 0 sabitini karşılaştırır. Karşılaştırma sonucu 1-bitlik boolean (`i1`) tipinde `"is_alive"` yazmacına atanır.
3. **Koşullu Dallanma Talimatı (`CreateCondBr`):**
   * `builder.CreateCondBr` metodu LLVM IR `br i1 %is_alive, label %alive, label %dead` talimatını üretir. `condValue` true ise işlemci `aliveBB` bloğuna, false ise `deadBB` bloğuna yönlendirilir.
4. **Blok İçi Kodlama ve Koşulsuz Atlamalar (`CreateBr`):**
   * `builder.SetInsertPoint(aliveBB)` ile imleç `aliveBB` içine alınır. İşlemler bitince `builder.CreateBr(mergeBB)` ile akış koşulsuz olarak birleşme noktasına (`mergeBB`) aktarılır.
   * Aynı işlem `deadBB` için tekrarlanarak her iki kolun da `mergeBB` üzerinde güvenle buluşması sağlanır.

---

## 2.5 Bellek Modeli, Yığın Tahsisi (`alloca`) ve `GetElementPtr` (GEP) Adres Hesaplama Algoritması

LLVM IR'da iki temel bellek erişim mantığı vardır:
1. **Yazmaç Tabanlı (Register-based SSA):** `add`, `sub`, `mul` gibi işlemler doğrudan sanal yazmaçlar üzerinde yürür.
2. **Bellek Tabanlı (Memory-based Alloca):** Yerel değişkenler yığında (`alloca`) tutulur, `load` ile okunur, `store` ile yazılır.

### Yığın Tahsisi (`alloca`) ve C++ API Kullanımı
SSA formunu elle yönetmek zor olduğu için ön yüz derleyicileri genellikle tüm yerel değişkenleri fonksiyonun giriş bloğunda `alloca` ile oluşturur.

#### C++ Tarafında `alloca` Tanımlanması:

```cpp
llvm::AllocaInst* healthAlloca = builder.CreateAlloca(builder.getInt32Ty(), nullptr, "player_health");

builder.CreateStore(builder.getInt32(100), healthAlloca);

llvm::Value* currentHealth = builder.CreateLoad(builder.getInt32Ty(), healthAlloca, "current_health_val");
```

#### Adım Adım Açıklama ve Çalışma İlkesi:

1. **Yığında Yer Ayırma (`CreateAlloca`):**
   * `builder.CreateAlloca` metodu, fonksiyonun yığın çeçevesinde (stack frame) 32-bitlik tamsayı saklayabilecek bir bellek adresi ayırır.
   * Dönen `healthAlloca` bir değer değil, o bellek alanını gösteren bir işaretçidir (`i32*`).
2. **Belleğe Değer Yazma (`CreateStore`):**
   * `builder.CreateStore` metodu, 100 tamsayı sabitini (`getInt32(100)`) yığındaki `healthAlloca` adresine kopyalar (`store i32 100, i32* %player_health`).
3. **Bellekten Değer Okuma (`CreateLoad`):**
   * `builder.CreateLoad` metodu, `healthAlloca` adresindeki veriyi okuyup yeni bir SSA yazmacı olan `currentHealth` içerisine yükler (`%current_health_val = load i32, i32* %player_health`).

LLVM'in `mem2reg` optimizasyon passi (Pass Manager), bu `alloca` bellek erişimlerini otomatik olarak SSA yazmaçlarına ve PHI düğümlerine dönüştürür!

---

### `GetElementPtr` (GEP) Talimatı ve C++ İle Struct/Array Adres Hesaplaması
`GetElementPtr` (GEP), LLVM IR'ın en kritik ve sıkça yanlış anlaşılan talimatlarından biridir. GEP **belleğe erişmez (load/store yapmaz)**, yalnızca bellek adresini (pointer arithmetic) hesaplar!

Oyun motorumuzda bir `Entity` yapısının bellek düzenini düşünelim:

```cpp
struct Entity {
    int id;            // 0. eleman (4 bayt)
    float health;      // 1. eleman (4 bayt)
    float position[3]; // 2. eleman (12 bayt: x, y, z)
};
```

#### C++ Tarafında `Entity` Yapısının Oluşturulması ve GEP Kullanımı:

```cpp
llvm::StructType* entityType = llvm::StructType::create(*context, "struct.Entity");

llvm::ArrayType* posArrayType = llvm::ArrayType::get(builder.getFloatTy(), 3);
entityType->setBody({builder.getInt32Ty(), builder.getFloatTy(), posArrayType});

std::vector<llvm::Value*> indices = {
    builder.getInt32(0),
    builder.getInt32(2),
    builder.getInt32(1)
};

llvm::Value* yCoordPtr = builder.CreateGEP(entityType, entityPtr, indices, "y_coord_ptr");

llvm::Value* yValue = builder.CreateLoad(builder.getFloatTy(), yCoordPtr, "y_val");
```

#### Adım Adım Açıklama ve Çalışma İlkesi:

1. **Struct Tipinin Tanımlanması (`StructType::create` ve `setBody`):**
   * LLVM'e `struct.Entity` isimli bir veri yapısı tanıtılır. `setBody` metoduna sırasıyla `i32` (id), `float` (health) ve `[3 x float]` (position dizisi) tipleri verilerek yapının bellek dizilimi belirlenir.
2. **GEP İndis Listesinin Mantığı (`indices`):**
   * **Birinci İndis (`builder.getInt32(0)`):** Base pointer ötelemesidir. `entityPtr[0]` anlamına gelir ve mevcut gösterici nesnesinin kendi adres başlangıcını seçer.
   * **İkinci İndis (`builder.getInt32(2)`):** Struct yapısı içerisindeki 2. indeksteki elemanı seçer (0: id, 1: health, 2: position).
   * **Üçüncü İndis (`builder.getInt32(1)`):** Seçilen `position` dizisi içerisindeki 1. indeksteki elemanı seçer (0: X, 1: Y, 2: Z koordinatı).
3. **GEP Adres Hesaplaması (`CreateGEP`):**
   * `builder.CreateGEP` çağrısı belleğe erişmeden yalnızca $y$ koordinatının yığındaki tam bayt adresini hesaplar ve bu adresi `yCoordPtr` işaretçisine atar.
4. **Verinin Okunması (`CreateLoad`):**
   * Hesaplanan `yCoordPtr` adresinden `float` değer okunarak `yValue` SSA yazmacına yüklenir.

---

## 2.6 `IRBuilder<>` Kullanımı ve AST'den LLVM IR Üretimi

Şimdi bu kavramları bir araya getirerek oyun dilimiz için tam teşekküllü bir AST yapısı ve bu AST'yi C++ LLVM API'si kullanarak adım adım LLVM IR'a dönüştüren derleyici kodunu yazalım.

### C++ Örneği: Tam Teşekküllü AST ve IR Generation (`src/frontend_ast.cpp`)

```cpp
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <vector>
#include <map>

static std::map<std::string, llvm::AllocaInst*> NamedValues;

class ExprAST {
public:
    virtual ~ExprAST() = default;
    virtual llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) = 0;
};

class NumberExprAST : public ExprAST {
    double val;
public:
    NumberExprAST(double val) : val(val) {}

    llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) override {
        return llvm::ConstantFP::get(context, llvm::APFloat(val));
    }
};

class VariableExprAST : public ExprAST {
    std::string name;
public:
    VariableExprAST(const std::string& name) : name(name) {}

    llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) override {
        llvm::AllocaInst* alloca = NamedValues[name];
        if (!alloca) {
            llvm::errs() << "HATA: Tanımsız değişken kullanımı: " << name << "\n";
            return nullptr;
        }
        return builder.CreateLoad(alloca->getAllocatedType(), alloca, name.c_str());
    }
};

class BinaryExprAST : public ExprAST {
    char op;
    std::unique_ptr<ExprAST> lhs, rhs;
public:
    BinaryExprAST(char op, std::unique_ptr<ExprAST> lhs, std::unique_ptr<ExprAST> rhs)
        : op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}

    llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) override {
        llvm::Value* l = lhs->codegen(context, module, builder);
        llvm::Value* r = rhs->codegen(context, module, builder);
        if (!l || !r) return nullptr;

        switch (op) {
            case '+': return builder.CreateFAdd(l, r, "addtmp");
            case '-': return builder.CreateFSub(l, r, "subtmp");
            case '*': return builder.CreateFMul(l, r, "multmp");
            default: return nullptr;
        }
    }
};

void generateSampleAST() {
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("GameASTModule", *context);
    llvm::IRBuilder<> builder(*context);

    auto expr = std::make_unique<BinaryExprAST>(
        '*',
        std::make_unique<BinaryExprAST>('+', std::make_unique<NumberExprAST>(10.0), std::make_unique<NumberExprAST>(20.0)),
        std::make_unique<NumberExprAST>(2.5)
    );

    llvm::FunctionType* ft = llvm::FunctionType::get(builder.getDoubleTy(), false);
    llvm::Function* func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "CalculateDamage", module.get());
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", func);
    builder.SetInsertPoint(bb);

    llvm::Value* result = expr->codegen(*context, *module, builder);
    builder.CreateRet(result);

    llvm::verifyFunction(*func);
    module->print(llvm::outs(), nullptr);
}
```

---

### Koda Adım Adım Derinlemesine Bakış ve Yürütme Algoritması

1. **`ExprAST::codegen` Sanal Fonksiyon Özyinelemesi (Recursion):**
   * AST ağacında kod üretimi Post-Order Traversal (Önce Sol, Sonra Sağ, Sonra Kök) sırasıyla yürür. `BinaryExprAST::codegen` çağrıldığında ilk olarak sol alt ağacın (`lhs->codegen`), ardından sağ alt ağacın (`rhs->codegen`) IR kodları üretilir.
2. **`NumberExprAST::codegen` İle Sabitlerin Üretimi:**
   * Yapıdaki sayısal değerler `llvm::ConstantFP::get` çağrısı ile `LLVMContext` deposunda tekilleştirilmiş sabit kayan noktalı sayılara dönüştürülür.
3. **`VariableExprAST::codegen` İle Değişken Okuma:**
   * Sembol tablosunda (`NamedValues`) değişkenin yığın adresi (`AllocaInst*`) bulunur. Bulunan adresten `CreateLoad` ile veri okunarak işlemci yazmacına aktarılır.
4. **`BinaryExprAST` Operatör Seçimi:**
   * Sol ve sağ alt ağaçlardan gelen `llvm::Value*` yazmaçları operatör karakterine göre (`+`, `-`, `*`) ilgili `CreateFAdd`, `CreateFSub` veya `CreateFMul` metoduna iletilerek matematiksel IR talimatı oluşturulur.

---

## 2.7 Bölüm Özeti ve Ön Yüz Mimarisi Değerlendirmesi

Bu bölümde, oyun programlama dilimizin Ön Yüz (Frontend) mimarisini baştan sona C++ LLVM API'lerini kullanarak ele aldık. Metinsel kaynak kodun **Soyut Sözdizim Ağacına (AST)** dönüştürülmesini, özyineli `codegen()` mimarisi ile bu ağacın LLVM IR'a aktarılmasını inceledik.

LLVM IR'ın bellek ve veri akışı omurgasını oluşturan **Static Single Assignment (SSA)** formunu, dallanmalardaki değer belirsizliklerini çözen **PHI Düğümlerini (`phi`)**, `CreateCondBr` ile kontrol akış grafiklerini (CFG) ve yığın tahsisi (`alloca`) ile bellek adres hesaplaması yapan **`GetElementPtr` (GEP)** talimatının C++ API ile adım adım kodlanmasını kavradık. Bir sonraki bölümde, ürettiğimiz bu ham LLVM IR'ı optimize etmek üzere Middle-End katmanına ve **New Pass Manager (NPM)** altyapısına geçeceğiz.
