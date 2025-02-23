; ModuleID = 'TestExamples/fib.c'
source_filename = "TestExamples/fib.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nofree nosync nounwind memory(none) uwtable
define dso_local i64 @fib(i64 noundef %n) local_unnamed_addr #0 {
entry:
  %x = alloca i64, align 8
  %syncreg = tail call token @llvm.syncregion.start()
  %cmp = icmp slt i64 %n, 2
  br i1 %cmp, label %return, label %if.end

if.end:                                           ; preds = %entry
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %x)
  detach within %syncreg, label %det.achd, label %det.cont

det.achd:                                         ; preds = %if.end
  %sub = add nsw i64 %n, -1
  %call = tail call i64 @fib(i64 noundef %sub)
  store i64 %call, ptr %x, align 8, !tbaa !5
  reattach within %syncreg, label %det.cont

det.cont:                                         ; preds = %det.achd, %if.end
  %sub1 = add nsw i64 %n, -2
  %call3 = 0
  sync within %syncreg, label %sync.continue

sync.continue:                                    ; preds = %det.cont
  %x.0.load11 = load i64, ptr %x, align 8
  %add = add nsw i64 %x.0.load11, %call3
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %x)
  br label %return

return:                                           ; preds = %entry, %sync.continue
  %retval.0 = phi i64 [ %add, %sync.continue ], [ %n, %entry ]
  ret i64 %retval.0
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #1

; Function Attrs: mustprogress nounwind willreturn memory(argmem: readwrite)
declare token @llvm.syncregion.start() #2

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #1

attributes #0 = { nofree nosync nounwind memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nounwind willreturn memory(argmem: readwrite) }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 16.0.6 (git@github.com:OpenCilk/opencilk-project.git 3f1a18993b8087b1b06c7e891dceec0c86016197)"}
!5 = !{!6, !6, i64 0}
!6 = !{!"long", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C/C++ TBAA"}
