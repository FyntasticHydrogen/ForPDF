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

## 2.3 Tekli Atama Biçimi (Static Single Assignment - SSA) ve PHI Düğümleri

### SSA Nedir ve Neden Hayatidir?
SSA formunda, her sanal yazmaca veya değişkene **yalnızca bir kez** değer atanabilir. Klasik bir programlama dilinde bir değişkene birden fazla kez değer atanabilir:

```c
// Klasik C/C++ Kodu (Gereksiz Değişken Yeniden Atamaları)
int health = 100;
health = health - damage;
health = health + healAmount;
```

Bu kod SSA yapısına dönüştürüldüğünde, her atama yeni bir SSA sürüm değişkeni (register) oluşturur:

```llvm
; LLVM IR SSA Biçimi
%health0 = copy i32 100
%health1 = sub i32 %health0, %damage
%health2 = add i32 %health1, %healAmount
```

* **Çalışma Algoritması ve Avantajı:** SSA formunun derleyicilere sağladığı en büyük avantaj, **Veri Akışı Analizini (Data Flow Analysis)** doğrusal hale getirmesidir. Bir yazmacın değerinin nereden geldiği ve nerede değiştiği tartışmasız bir şekilde bellidir. Derleyici, karmaşık gösterici (pointer) takibi yapmadan bağımlılıkları görür.
* **Benzetme:** SSA formunu bir muhasebe defterindeki **Silinemez Günlük Kayıtlara (Immutable Ledger / Blockchain)** benzetebiliriz. Eski bir girdinin üzerini çizip değiştiremezsiniz; her yeni durum için yeni bir satır kaydı açarsınız.

### PHI Düğümleri (`phi` instruction)
Dallanma (if-else, döngüler) olan durumlarda bir değişkenin hangi kontrol akışı yolundan geldiği çalışma zamanında belli olur. SSA kuralını bozmadan bu durumu çözmek için **PHI Düğümleri (`phi` instruction)** kullanılır.

```llvm
; Oyuncu Can Durumu Örneği
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
  ; PHI Düğümü: Kodun hangi bloktan geldiğine göre doğru hp değerini seçer
  %final_hp = phi i32 [ %hp_after_hit, %take_damage ], [ %hp_after_block, %blocked ]
  ret i32 %final_hp
```

<div class="callout callout-warning">
<div class="callout-title">PHI Düğümü Kuralı</div>
LLVM IR içerisinde bir PHI düğümü talimatı (`phi`), bulunacağı Basic Block'un **en başında** (diğer tüm işlem talimatlarından önce) yer almak zorundadır!
</div>

---

## 2.4 Temel Yapı Taşları (Basic Blocks) ve Kontrol Akış Grafiği (CFG)

Bir **Temel Yapı Taşı (Basic Block)**, içerisine yalnızca tek bir noktadan girilen (ilk talimat) ve yalnızca tek bir noktadan çıkılan (son talimat) talimatlar dizisidir.

Bir Basic Block'un son talimatı mutlaka bir **Terminator Instruction** (Sonlandırıcı Talimat) olmalıdır. Örnek sonlandırıcılar:
* `ret`: Fonksiyondan döner.
* `br`: Başka bir Basic Block'a dallanır (koşullu veya koşulsuz).
* `switch`: Çoklu dallanma yapar.

Basic Block'ların birbirine `br` (branch) talimatları ile bağlanmasıyla **Kontrol Akış Grafiği (Control Flow Graph - CFG)** elde edilir. Derleyici optimizasyonları (döngü tespiti, ölü kod eleme vb.) bu CFG üzerinde yürütülür.

---

## 2.5 Bellek Modeli ve `GetElementPtr` (GEP) Adres Hesaplama Algoritması

LLVM IR'da iki temel bellek erişim mantığı vardır:
1. **Yazmaç Tabanlı (Register-based SSA):** `add`, `sub`, `mul` gibi işlemler doğrudan sanal yazmaçlar üzerinde yürür.
2. **Bellek Tabanlı (Memory-based Alloca):** Yerel değişkenler yığında (`alloca`) tutulur, `load` ile okunur, `store` ile yazılır.

### Yığın Tahsisi (`alloca`)
SSA formunu doğrudan elle yönetmek zor olduğu için ön yüz derleyicileri genellikle tüm yerel değişkenleri yığında `alloca` ile oluşturur.

```llvm
%player_health = alloca i32, align 4
store i32 100, i32* %player_health, align 4
%val = load i32, i32* %player_health, align 4
```

LLVM'in `mem2reg` optimizasyon passi (Pass Manager), bu `alloca` bellek erişimlerini otomatik olarak SSA yazmaçlarına ve PHI düğümlerine dönüştürür!

### `GetElementPtr` (GEP) Talimatı
`GetElementPtr` (GEP), LLVM IR'ın en kritik ve sıkça yanlış anlaşılan talimatlarından biridir. GEP **belleğe erişmez (load/store yapmaz)**, yalnızca bellek adresini (pointer arithmetic) hesaplar!

Oyun motorumuzda bir `Entity` yapısının bellek düzenini düşünelim:

```cpp
struct Entity {
    int id;          // 0. eleman (4 bayt)
    float health;    // 1. eleman (4 bayt)
    float position[3]; // 2. eleman (12 bayt: x, y, z)
};
```

LLVM IR üzerinde bir `Entity*` göstericisinden oyuncunun `position[1]` (y koordinatı) adresini hesaplamak için GEP şu şekilde kullanılır:

```llvm
; %entity_ptr: Entity* türünde bir pointer
; 1. indis '0': Pointer'ın kendisi üzerinden dizide ilerleme (0 offset)
; 2. indis '2': Struct içindeki 2. alan (position dizisi)
; 3. indis '1': position dizisinin 1. elemanı (y koordinatı)

%y_ptr = getelementptr inbounds %struct.Entity, %struct.Entity* %entity_ptr, i32 0, i32 2, i32 1
%y_val = load float, float* %y_ptr, align 4
```

* **Adres Hesaplama Formülü:**
$$\text{Hedef Adres} = \text{Taban Adres} + (0 \times \text{sizeof}(Entity)) + \text{offsetof}(Entity, position) + (1 \times \text{sizeof}(float))$$

<div class="callout callout-info">
<div class="callout-title">GEP Neden İlk İndis Olarak '0' Alır?</div>
C/C++ dilindeki `entity_ptr->position[1]` ifadesinde, `entity_ptr` aslında bir dizi nesnesinin ilk elemanının adresidir (`*(entity_ptr + 0)`). İlk indis olan `0`, pointer seviyesinde kaç eleman öteye gidileceğini belirler.
</div>

---

## 2.6 `IRBuilder<>` Kullanımı ve AST'den LLVM IR Üretimi

Şimdi bu kavramları bir araya getirerek oyun dilimiz için bir AST yapısı ve bu AST'yi LLVM IR'a dönüştüren C++ kodunu yazalım.

### C++ Örneği: AST ve IR Generation (`src/frontend_ast.cpp`)

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

// Taban AST Sınıfı
class ExprAST {
public:
    virtual ~ExprAST() = default;
    virtual llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) = 0;
};

// Sayısal Sabit AST Düğümü
class NumberExprAST : public ExprAST {
    double val;
public:
    NumberExprAST(double val) : val(val) {}

    llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) override {
        return llvm::ConstantFP::get(context, llvm::APFloat(val));
    }
};

// Değişken Okuma AST Düğümü
class VariableExprAST : public ExprAST {
    std::string name;
public:
    VariableExprAST(const std::string& name) : name(name) {}

    llvm::Value* codegen(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder) override {
        // Not: Gerçek derleyicilerde Sembol Tablosundan (Symbol Table) okunur
        return nullptr;
    }
};

// İkili İşlem (Binary Expression: +, -, *) AST Düğümü
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

// Kullanım Örneği
void generateSampleAST() {
    llvm::LLVMContext context;
    llvm::Module module("GameASTModule", context);
    llvm::IRBuilder<> builder(context);

    // İfade: (10.0 + 20.0) * 2.5 (Örn: Oyun içi hasar çarpanı hesabı)
    auto expr = std::make_unique<BinaryExprAST>(
        '*',
        std::make_unique<BinaryExprAST>('+', std::make_unique<NumberExprAST>(10.0), std::make_unique<NumberExprAST>(20.0)),
        std::make_unique<NumberExprAST>(2.5)
    );

    // Dummy test fonksiyonu
    llvm::FunctionType* ft = llvm::FunctionType::get(builder.getDoubleTy(), false);
    llvm::Function* func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "CalculateDamage", &module);
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", func);
    builder.SetInsertPoint(bb);

    llvm::Value* result = expr->codegen(context, module, builder);
    builder.CreateRet(result);

    llvm::verifyFunction(*func);
    module.print(llvm::outs(), nullptr);
}
```

---

### Koda Adım Adım Derinlemesine Bakış ve Yürütme Algorithması

1. **`ExprAST::codegen` Sanal Fonksiyon Özyinelemesi (Recursion):**
   * *Çalışma Mantığı:* Ağacın en altındaki (leaf nodes) sabitler (`NumberExprAST`) önce ziyaret edilir. Alt düğümlerden dönen `llvm::Value*` göstericileri üst düğümlere (`BinaryExprAST`) girdi olarak iletilir. Bu yöntem Post-Order Traversal (Önce Sol, Sonra Sağ, Sonra Kök) algoritmasıdır.
2. **`builder.CreateFAdd` / `CreateFMul`:**
   * *Çalışma Mantığı:* İki float değeri toplayan veya çarpan LLVM IR talimatlarını oluşturur ve `addtmp`/`multmp` isimli sanal yazmaçlara atar.
3. **`builder.CreateRet(result)`:**
   * *Çalışma Mantığı:* Bloğun sonlandırıcı talimatını (Terminator) ekler ve hesaplanan nihai değeri fonksiyondan döndürür.

---

## 2.7 Bölüm Özeti ve Ön Yüz Mimarisi Değerlendirmesi

Bu bölümde, oyun programlama dilimizin Ön Yüz (Frontend) mimarisini baştan sona ele aldık. Metinsel kaynak kodun **Soyut Sözdizim Ağacına (AST)** dönüştürülmesini, özyineli `codegen()` mimarisi ile bu ağacın LLVM IR'a aktarılmasını inceledik.

LLVM IR'ın bellek ve veri akışı omurgasını oluşturan **Static Single Assignment (SSA)** formunu, dallanmalardaki değer belirsizliklerini çözen **PHI Düğümlerini (`phi`)**, kontrol akış grafiklerini (CFG) ve yığın tahsisi (`alloca`) ile bellek adres hesaplaması yapan **`GetElementPtr` (GEP)** talimatının matematiksel arka planını kavradık. Bir sonraki bölümde, ürettiğimiz bu ham LLVM IR'ı optimize etmek üzere Middle-End katmanına ve **New Pass Manager (NPM)** altyapısına geçeceğiz.
